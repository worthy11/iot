#include "telemetry_service.h"
#include "common.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gatt.h"
#include <string.h>
#include <stdint.h>

static const char *TAG = "telemetry_svc";

static const ble_uuid128_t TELEMETRY_SVC_UUID = BLE_UUID128_INIT(0xd0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t TEMP_CHR_UUID = BLE_UUID128_INIT(0xd1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PH_CHR_UUID = BLE_UUID128_INIT(0xd2, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t FEED_CHR_UUID = BLE_UUID128_INIT(0xd3, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static uint16_t s_temp_chr_val_handle;
static uint16_t s_ph_chr_val_handle;
static uint16_t s_feed_chr_val_handle;

// Current values to be sent via notifications
static float s_current_temp = 0.0f;
static float s_current_ph = 0.0f;
static uint8_t s_current_feed = 0;

static int telemetry_read_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg);
static int telemetry_desc_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg);
static int telemetry_cccd_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def telemetry_svc_defs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &TELEMETRY_SVC_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &TEMP_CHR_UUID.u,
                .access_cb = telemetry_read_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_temp_chr_val_handle,
                .descriptors = (struct ble_gatt_dsc_def[]){
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = telemetry_desc_cb,
                        .arg = "Temperature",
                    },
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2902), // CCCD
                        .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE,
                        .access_cb = telemetry_cccd_cb,
                    },
                    {0},
                },
            },
            {
                .uuid = &PH_CHR_UUID.u,
                .access_cb = telemetry_read_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_ph_chr_val_handle,
                .descriptors = (struct ble_gatt_dsc_def[]){
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = telemetry_desc_cb,
                        .arg = "pH",
                    },
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2902), // CCCD
                        .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE,
                        .access_cb = telemetry_cccd_cb,
                    },
                    {0},
                },
            },
            {
                .uuid = &FEED_CHR_UUID.u,
                .access_cb = telemetry_read_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_feed_chr_val_handle,
                .descriptors = (struct ble_gatt_dsc_def[]){
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = telemetry_desc_cb,
                        .arg = "Feeding",
                    },
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2902), // CCCD
                        .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE,
                        .access_cb = telemetry_cccd_cb,
                    },
                    {0},
                },
            },
            {0},
        },
    },
    {0},
};

static int telemetry_desc_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const char *desc = (const char *)arg;
    return os_mbuf_append(ctxt->om, desc, strlen(desc));
}

static int telemetry_cccd_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)arg;
    uint16_t value;
    int rc;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_DSC)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }

    rc = ble_hs_mbuf_to_flat(ctxt->om, &value, sizeof(value), NULL);
    if (rc != 0)
    {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    ESP_LOGI(TAG, "CCCD write: conn_handle=%d, value=0x%04x", conn_handle, value);
    return 0;
}

static int telemetry_read_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    const ble_uuid_t *uuid = ctxt->chr->uuid;
    int rc;

    if (ble_uuid_cmp(uuid, &TEMP_CHR_UUID.u) == 0)
    {
        rc = os_mbuf_append(ctxt->om, &s_current_temp, sizeof(float));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    else if (ble_uuid_cmp(uuid, &PH_CHR_UUID.u) == 0)
    {
        rc = os_mbuf_append(ctxt->om, &s_current_ph, sizeof(float));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    else if (ble_uuid_cmp(uuid, &FEED_CHR_UUID.u) == 0)
    {
        rc = os_mbuf_append(ctxt->om, &s_current_feed, sizeof(uint8_t));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return 0;
}

static void notify_value(uint16_t conn_handle, uint16_t val_handle)
{
    int rc;

    // ble_gatts_notify sends a notification with the current characteristic value
    // The value is provided by the read callback (telemetry_read_cb)
    rc = ble_gatts_notify(conn_handle, val_handle);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to send notification: %d", rc);
    }
}

static void notify_all_connections(uint16_t val_handle)
{
    // Notify all connected devices
    // Note: NimBLE typically supports one connection at a time, but we check for handle 0
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(0, &desc) == 0)
    {
        notify_value(desc.conn_handle, val_handle);
    }
}

void telemetry_service_notify_temperature(float temperature)
{
    s_current_temp = temperature;
    notify_all_connections(s_temp_chr_val_handle);
}

void telemetry_service_notify_ph(float ph)
{
    s_current_ph = ph;
    notify_all_connections(s_ph_chr_val_handle);
}

void telemetry_service_notify_feed(bool success)
{
    s_current_feed = success ? 1 : 0;
    notify_all_connections(s_feed_chr_val_handle);
}

const struct ble_gatt_svc_def *telemetry_service_get_svc_def(void)
{
    return telemetry_svc_defs;
}
