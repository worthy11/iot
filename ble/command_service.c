#include "command_service.h"
#include "common.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "event_manager.h"
#include "hardware/hardware_manager.h"
#include <string.h>
#include <stdint.h>

static const char *TAG = "command_svc";

// Characteristic UUIDs
static const ble_uuid128_t COMMAND_SVC_UUID = BLE_UUID128_INIT(0xc0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t FORCE_FEED_CHR_UUID = BLE_UUID128_INIT(0xc1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t FORCE_TEMP_CHR_UUID = BLE_UUID128_INIT(0xc2, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t FORCE_PH_CHR_UUID = BLE_UUID128_INIT(0xc3, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t TEMP_INTERVAL_CHR_UUID = BLE_UUID128_INIT(0xc4, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t FEED_INTERVAL_CHR_UUID = BLE_UUID128_INIT(0xc5, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PUBLISH_INTERVAL_CHR_UUID = BLE_UUID128_INIT(0xc6, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static int command_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg);
static int command_desc_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def command_svc_defs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &COMMAND_SVC_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &FORCE_FEED_CHR_UUID.u,
                .access_cb = command_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = command_desc_cb,
                        .arg = "Force Feed",
                    },
                    {0},
                },
            },
            {
                .uuid = &FORCE_TEMP_CHR_UUID.u,
                .access_cb = command_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = command_desc_cb,
                        .arg = "Force Temp",
                    },
                    {0},
                },
            },
            {
                .uuid = &FORCE_PH_CHR_UUID.u,
                .access_cb = command_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = command_desc_cb,
                        .arg = "Force pH",
                    },
                    {0},
                },
            },
            {
                .uuid = &TEMP_INTERVAL_CHR_UUID.u,
                .access_cb = command_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = command_desc_cb,
                        .arg = "Temp Interval",
                    },
                    {0},
                },
            },
            {
                .uuid = &FEED_INTERVAL_CHR_UUID.u,
                .access_cb = command_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = command_desc_cb,
                        .arg = "Feed Interval",
                    },
                    {0},
                },
            },
            {
                .uuid = &PUBLISH_INTERVAL_CHR_UUID.u,
                .access_cb = command_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901),
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = command_desc_cb,
                        .arg = "Publish Interval",
                    },
                    {0},
                },
            },
            {0},
        },
    },
    {0},
};

static int command_desc_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    const char *desc = (const char *)arg;
    return os_mbuf_append(ctxt->om, desc, strlen(desc));
}

static int command_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)arg;

    const ble_uuid_t *uuid = ctxt->chr->uuid;
    int rc;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        // Read operation
        if (ble_uuid_cmp(uuid, &FORCE_FEED_CHR_UUID.u) == 0)
        {
            // Return status (0 = idle, 1 = active)
            uint8_t status = 0;
            rc = os_mbuf_append(ctxt->om, &status, sizeof(status));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        else if (ble_uuid_cmp(uuid, &FORCE_TEMP_CHR_UUID.u) == 0)
        {
            // Return status (0 = idle, 1 = active)
            uint8_t status = 0;
            rc = os_mbuf_append(ctxt->om, &status, sizeof(status));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        else if (ble_uuid_cmp(uuid, &FORCE_PH_CHR_UUID.u) == 0)
        {
            // Return status (0 = idle, 1 = active)
            uint8_t status = 0;
            rc = os_mbuf_append(ctxt->om, &status, sizeof(status));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        else if (ble_uuid_cmp(uuid, &TEMP_INTERVAL_CHR_UUID.u) == 0)
        {
            // Return current temp interval
            uint32_t interval = event_manager_get_temp_reading_interval();
            rc = os_mbuf_append(ctxt->om, &interval, sizeof(interval));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        else if (ble_uuid_cmp(uuid, &FEED_INTERVAL_CHR_UUID.u) == 0)
        {
            // Return current feed interval
            uint32_t interval = event_manager_get_feeding_interval();
            rc = os_mbuf_append(ctxt->om, &interval, sizeof(interval));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        else if (ble_uuid_cmp(uuid, &PUBLISH_INTERVAL_CHR_UUID.u) == 0)
        {
            // Return current publish interval
            uint32_t interval = event_manager_get_publish_interval();
            rc = os_mbuf_append(ctxt->om, &interval, sizeof(interval));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
    }
    else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        // Write operation
        struct os_mbuf *om = ctxt->om;
        uint8_t buf[16];
        int len = OS_MBUF_PKTLEN(om);

        if (len == 0)
        {
            ESP_LOGW(TAG, "Command write is empty");
            return 0;
        }

        if (len > (int)sizeof(buf))
        {
            len = sizeof(buf);
            ESP_LOGW(TAG, "Command write truncated to %d bytes", len);
        }

        ble_hs_mbuf_to_flat(om, buf, len, NULL);

        if (ble_uuid_cmp(uuid, &FORCE_FEED_CHR_UUID.u) == 0)
        {
            ESP_LOGI(TAG, "Force feed command");
            event_manager_set_bits(EVENT_BIT_FEED_SCHEDULED);
            return 0;
        }
        else if (ble_uuid_cmp(uuid, &FORCE_TEMP_CHR_UUID.u) == 0)
        {
            ESP_LOGI(TAG, "Force temp command");
            event_manager_set_bits(EVENT_BIT_TEMP_SCHEDULED);
            return 0;
        }
        else if (ble_uuid_cmp(uuid, &FORCE_PH_CHR_UUID.u) == 0)
        {
            ESP_LOGI(TAG, "Force pH command");
            event_manager_set_bits(EVENT_BIT_PH_SCHEDULED);
            return 0;
        }
        else if (ble_uuid_cmp(uuid, &TEMP_INTERVAL_CHR_UUID.u) == 0)
        {
            if (len >= 4)
            {
                uint32_t interval;
                memcpy(&interval, buf, sizeof(uint32_t));
                ESP_LOGI(TAG, "Change temp interval: %lu seconds", (unsigned long)interval);
                event_manager_set_temp_reading_interval(interval);
            }
            else
            {
                ESP_LOGW(TAG, "Change temp interval command too short");
            }
            return 0;
        }
        else if (ble_uuid_cmp(uuid, &FEED_INTERVAL_CHR_UUID.u) == 0)
        {
            if (len >= 4)
            {
                uint32_t interval;
                memcpy(&interval, buf, sizeof(uint32_t));
                ESP_LOGI(TAG, "Change feed interval: %lu seconds", (unsigned long)interval);
                event_manager_set_feeding_interval(interval);
            }
            else
            {
                ESP_LOGW(TAG, "Change feed interval command too short");
            }
            return 0;
        }
        else if (ble_uuid_cmp(uuid, &PUBLISH_INTERVAL_CHR_UUID.u) == 0)
        {
            if (len >= 4)
            {
                uint32_t interval;
                memcpy(&interval, buf, sizeof(uint32_t));
                ESP_LOGI(TAG, "Change publish interval: %lu seconds", (unsigned long)interval);
                event_manager_set_publish_interval((int)interval);
            }
            else
            {
                ESP_LOGW(TAG, "Change publish interval command too short");
            }
            return 0;
        }
    }

    return BLE_ATT_ERR_UNLIKELY;
}

const struct ble_gatt_svc_def *command_service_get_svc_def(void)
{
    return command_svc_defs;
}
