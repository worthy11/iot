#include "wifi_config_service.h"
#include "common.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "wifi_manager.h"
#include "event_manager.h"
#include "utils/nvs_utils.h"
#include <string.h>

static const char *TAG = "wifi_cfg_svc";

static char s_pending_ssid[32] = {0};
static char s_pending_pass[64] = {0};

static const ble_uuid128_t WIFI_SVC_UUID = BLE_UUID128_INIT(0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t WIFI_SSID_UUID = BLE_UUID128_INIT(0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t WIFI_PASS_UUID = BLE_UUID128_INIT(0xf2, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t WIFI_APPLY_UUID = BLE_UUID128_INIT(0xf3, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static int wifi_config_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg);

static int wifi_config_ssid_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                      struct ble_gatt_access_ctxt *ctxt, void *arg);

static int wifi_desc_cb(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def wifi_svc_defs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &WIFI_SVC_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &WIFI_SSID_UUID.u,
                .access_cb = wifi_config_ssid_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){
                    {.uuid = BLE_UUID16_DECLARE(0x2901),
                     .att_flags = BLE_ATT_F_READ,
                     .min_key_size = 0,
                     .access_cb = wifi_desc_cb,
                     .arg = "SSID"},
                    {0}},
            },
            {
                .uuid = &WIFI_PASS_UUID.u,
                .access_cb = wifi_config_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2901), .att_flags = BLE_ATT_F_READ, .min_key_size = 0, .access_cb = wifi_desc_cb, .arg = "Password"}, {0}},
            },
            {
                .uuid = &WIFI_APPLY_UUID.u,
                .access_cb = wifi_config_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2901), .att_flags = BLE_ATT_F_READ, .min_key_size = 0, .access_cb = wifi_desc_cb, .arg = "Apply"}, {0}},
            },
            {0},
        },
    },
    {0},
};
static int wifi_desc_cb(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const char *desc = (const char *)arg;
    return os_mbuf_append(ctxt->om, desc, strlen(desc));
}

static int wifi_config_ssid_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    const ble_uuid_t *uuid = ctxt->chr->uuid;

    if (ble_uuid_cmp(uuid, &WIFI_SSID_UUID.u) == 0)
    {
        switch (ctxt->op)
        {
        case BLE_GATT_ACCESS_OP_READ_CHR:
        {
            const char *ssid_to_return = NULL;

            if (strlen(s_pending_ssid) > 0)
            {
                ssid_to_return = s_pending_ssid;
            }
            else
            {
                ssid_to_return = wifi_manager_get_current_ssid();
            }

            if (ssid_to_return != NULL && strlen(ssid_to_return) > 0)
            {
                int rc = os_mbuf_append(ctxt->om, ssid_to_return, strlen(ssid_to_return));
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            else
            {
                return 0;
            }
        }
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
        {
            struct os_mbuf *om = ctxt->om;
            uint8_t buf[64];
            int len = OS_MBUF_PKTLEN(om);

            if (len >= (int)sizeof(buf))
            {
                len = sizeof(buf) - 1;
            }

            ble_hs_mbuf_to_flat(om, buf, len, NULL);
            buf[len] = '\0';

            size_t copy_len = len < (int)sizeof(s_pending_ssid) - 1 ? (size_t)len : sizeof(s_pending_ssid) - 1;
            memcpy(s_pending_ssid, buf, copy_len);
            s_pending_ssid[copy_len] = '\0';
            ESP_LOGI(TAG, "SSID set to '%s' (pending, not saved yet)", s_pending_ssid);
            return 0;
        }
        default:
            return BLE_ATT_ERR_UNLIKELY;
        }
    }

    return BLE_ATT_ERR_UNLIKELY;
}
static int wifi_config_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    const ble_uuid_t *uuid = ctxt->chr->uuid;
    struct os_mbuf *om = ctxt->om;
    uint8_t buf[64];
    int len = OS_MBUF_PKTLEN(om);

    if (len >= (int)sizeof(buf))
    {
        len = sizeof(buf) - 1;
    }

    ble_hs_mbuf_to_flat(om, buf, len, NULL);
    buf[len] = '\0';

    if (ble_uuid_cmp(uuid, &WIFI_SSID_UUID.u) == 0)
    {
        size_t copy_len = len < (int)sizeof(s_pending_ssid) - 1 ? (size_t)len : sizeof(s_pending_ssid) - 1;
        memcpy(s_pending_ssid, buf, copy_len);
        s_pending_ssid[copy_len] = '\0';
        ESP_LOGI(TAG, "SSID received (pending)");
    }
    else if (ble_uuid_cmp(uuid, &WIFI_PASS_UUID.u) == 0)
    {
        size_t copy_len = len < (int)sizeof(s_pending_pass) - 1 ? (size_t)len : sizeof(s_pending_pass) - 1;
        memcpy(s_pending_pass, buf, copy_len);
        s_pending_pass[copy_len] = '\0';
        ESP_LOGI(TAG, "Password received (pending)");
    }
    else if (ble_uuid_cmp(uuid, &WIFI_APPLY_UUID.u) == 0)
    {
        esp_err_t err = nvs_save_wifi_credentials(s_pending_ssid, s_pending_pass);
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "WiFi credentials saved successfully");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to save WiFi credentials: %s", esp_err_to_name(err));
        }
        event_manager_clear_bits(EVENT_BIT_CONFIG_MODE);
        event_manager_set_bits(EVENT_BIT_PROVISION_TRIGGER);
    }

    return 0;
}

const struct ble_gatt_svc_def *wifi_config_service_get_svc_def(void)
{
    return wifi_svc_defs;
}