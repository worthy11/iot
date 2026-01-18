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
                    {0},
                },
                // Note: CCCD (0x2902) is automatically added by NimBLE when BLE_GATT_CHR_F_NOTIFY flag is set
            },
            {
                .uuid = &PH_CHR_UUID.u, .access_cb = telemetry_read_cb, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_ph_chr_val_handle, .descriptors = (struct ble_gatt_dsc_def[]){
                                                                                                                                                                      {
                                                                                                                                                                          .uuid = BLE_UUID16_DECLARE(0x2901),
                                                                                                                                                                          .att_flags = BLE_ATT_F_READ,
                                                                                                                                                                          .access_cb = telemetry_desc_cb,
                                                                                                                                                                          .arg = "pH",
                                                                                                                                                                      },
                                                                                                                                                                      {0},
                                                                                                                                                                  },
                // Note: CCCD (0x2902) is automatically added by NimBLE when BLE_GATT_CHR_F_NOTIFY flag is set
            },
            {
                .uuid = &FEED_CHR_UUID.u, .access_cb = telemetry_read_cb, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_feed_chr_val_handle, .descriptors = (struct ble_gatt_dsc_def[]){
                                                                                                                                                                          {
                                                                                                                                                                              .uuid = BLE_UUID16_DECLARE(0x2901),
                                                                                                                                                                              .att_flags = BLE_ATT_F_READ,
                                                                                                                                                                              .access_cb = telemetry_desc_cb,
                                                                                                                                                                              .arg = "Feeding",
                                                                                                                                                                          },
                                                                                                                                                                          {0},
                                                                                                                                                                      },
                // Note: CCCD (0x2902) is automatically added by NimBLE when BLE_GATT_CHR_F_NOTIFY flag is set
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
    const char *char_name = "Unknown";
    uint16_t char_val_handle = 0;

    // Identify which characteristic this CCCD belongs to
    const ble_uuid_t *uuid = ctxt->chr->uuid;
    if (ble_uuid_cmp(uuid, &TEMP_CHR_UUID.u) == 0)
    {
        char_name = "Temperature";
        char_val_handle = s_temp_chr_val_handle;
    }
    else if (ble_uuid_cmp(uuid, &PH_CHR_UUID.u) == 0)
    {
        char_name = "pH";
        char_val_handle = s_ph_chr_val_handle;
    }
    else if (ble_uuid_cmp(uuid, &FEED_CHR_UUID.u) == 0)
    {
        char_name = "Feed";
        char_val_handle = s_feed_chr_val_handle;
    }

    // Log all CCCD operations at the start
    ESP_LOGI(TAG, "CCCD callback: conn_handle=%d, characteristic='%s', char_val_handle=%d, cccd_attr_handle=%d, operation=%d",
             conn_handle, char_name, char_val_handle, attr_handle, ctxt->op);

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC)
    {
        // Reading CCCD - NimBLE handles this automatically, but we log it
        // The actual value will be returned by NimBLE's CCCD storage
        ESP_LOGI(TAG, "CCCD read: conn_handle=%d, characteristic='%s', cccd_attr_handle=%d",
                 conn_handle, char_name, attr_handle);
        return 0;
    }
    else if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_DSC)
    {
        ESP_LOGW(TAG, "CCCD unexpected operation: %d, characteristic='%s', attr_handle=%d",
                 ctxt->op, char_name, attr_handle);
        return BLE_ATT_ERR_UNLIKELY;
    }

    // Write operation - parse the value
    rc = ble_hs_mbuf_to_flat(ctxt->om, &value, sizeof(value), NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "CCCD write failed to read value: %d, characteristic='%s', attr_handle=%d",
                 rc, char_name, attr_handle);
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    // Check if notifications are enabled (bit 0) or indications (bit 1)
    bool notify_enabled = (value & 0x0001) != 0;
    bool indicate_enabled = (value & 0x0002) != 0;

    if (notify_enabled || indicate_enabled)
    {
        ESP_LOGI(TAG, "✓ Subscription ENABLED: conn_handle=%d, characteristic='%s', char_val_handle=%d, cccd_attr_handle=%d, cccd_value=0x%04x (notify=%s, indicate=%s)",
                 conn_handle, char_name, char_val_handle, attr_handle, value,
                 notify_enabled ? "YES" : "NO", indicate_enabled ? "YES" : "NO");
    }
    else
    {
        ESP_LOGI(TAG, "✗ Subscription DISABLED: conn_handle=%d, characteristic='%s', char_val_handle=%d, cccd_attr_handle=%d, cccd_value=0x%04x",
                 conn_handle, char_name, char_val_handle, attr_handle, value);
    }

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
    const char *char_name = "Unknown";

    // Identify which characteristic is being notified
    if (val_handle == s_temp_chr_val_handle)
    {
        char_name = "Temperature";
    }
    else if (val_handle == s_ph_chr_val_handle)
    {
        char_name = "pH";
    }
    else if (val_handle == s_feed_chr_val_handle)
    {
        char_name = "Feed";
    }

    // ble_gatts_notify sends a notification with the current characteristic value
    // The value is provided by the read callback (telemetry_read_cb)
    rc = ble_gatts_notify(conn_handle, val_handle);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to send notification: conn_handle=%d, characteristic='%s', val_handle=%d, error=%d",
                 conn_handle, char_name, val_handle, rc);
    }
    else
    {
        ESP_LOGD(TAG, "Notification sent: conn_handle=%d, characteristic='%s', val_handle=%d",
                 conn_handle, char_name, val_handle);
    }
}

static void notify_all_connections(uint16_t val_handle)
{
    // Notify all connected devices
    // Note: NimBLE typically supports one connection at a time, but we check for handle 0
    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(0, &desc);
    if (rc == 0)
    {
        notify_value(desc.conn_handle, val_handle);
    }
    else
    {
        ESP_LOGD(TAG, "No active connections to notify (val_handle=%d)", val_handle);
    }
}

void telemetry_service_notify_temperature(float temperature)
{
    s_current_temp = temperature;
    ESP_LOGI(TAG, "Sending temperature notification: %.2f°C", temperature);
    notify_all_connections(s_temp_chr_val_handle);
}

void telemetry_service_notify_ph(float ph)
{
    s_current_ph = ph;
    ESP_LOGI(TAG, "Sending pH notification: %.2f", ph);
    notify_all_connections(s_ph_chr_val_handle);
}

void telemetry_service_notify_feed(bool success)
{
    s_current_feed = success ? 1 : 0;
    ESP_LOGI(TAG, "Sending feed notification: %s", success ? "success" : "failed");
    notify_all_connections(s_feed_chr_val_handle);
}

const struct ble_gatt_svc_def *telemetry_service_get_svc_def(void)
{
    return telemetry_svc_defs;
}

const char *telemetry_service_get_char_name(uint16_t attr_handle)
{
    if (attr_handle == s_temp_chr_val_handle)
    {
        return "Temperature";
    }
    else if (attr_handle == s_ph_chr_val_handle)
    {
        return "pH";
    }
    else if (attr_handle == s_feed_chr_val_handle)
    {
        return "Feed";
    }
    return "Unknown";
}
