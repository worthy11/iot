#include "wifi_config_service.h"
#include "common.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "wifi_manager.h"
#include "event_manager.h"
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

static int wifi_format_desc_cb(uint16_t conn_handle, uint16_t attr_handle,
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

static int wifi_format_desc_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    // Characteristic Format descriptor (0x2904)
    // Format: UTF-8 string (0x19)
    // Exponent: 0x00
    // Unit: Unitless (0x2700)
    // Namespace: Bluetooth SIG (0x01)
    // Description: 0x0000
    uint8_t format_desc[] = {
        0x19,       // Format: UTF-8 string
        0x00,       // Exponent
        0x00, 0x27, // Unit: 0x2700 (unitless)
        0x01,       // Namespace: Bluetooth SIG
        0x00, 0x00  // Description
    };
    return os_mbuf_append(ctxt->om, format_desc, sizeof(format_desc));
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

            // Return pending SSID if set, otherwise return current saved SSID
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
                // Return empty string if no SSID is set
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

    if (ble_uuid_cmp(uuid, &WIFI_PASS_UUID.u) == 0)
    {
        size_t copy_len = len < (int)sizeof(s_pending_pass) - 1 ? (size_t)len : sizeof(s_pending_pass) - 1;
        memcpy(s_pending_pass, buf, copy_len);
        s_pending_pass[copy_len] = '\0';
        ESP_LOGI(TAG, "Password received");
    }
    else if (ble_uuid_cmp(uuid, &WIFI_APPLY_UUID.u) == 0)
    {
        if (len > 0 && buf[0] == 0x01)
        {
            const char *current_ssid = wifi_manager_get_current_ssid();
            const char *current_pass = wifi_manager_get_current_password();

            const char *ssid_to_use = (strlen(s_pending_ssid) > 0) ? s_pending_ssid : current_ssid;
            const char *pass_to_use = (strlen(s_pending_pass) > 0) ? s_pending_pass : current_pass;

            // Use empty strings if nothing is available - this will clear credentials
            const char *final_ssid = ssid_to_use ? ssid_to_use : "";
            const char *final_pass = pass_to_use ? pass_to_use : "";

            ESP_LOGI(TAG, "CONNECT=1 -> saving WiFi credentials (SSID: '%s')", final_ssid);
            esp_err_t err = wifi_manager_save_credentials(final_ssid, final_pass);
            if (err == ESP_OK)
            {
                ESP_LOGI(TAG, "WiFi credentials saved successfully.");
                event_manager_set_bits(EVENT_BIT_WIFI_CONFIG_SAVED);
            }
            else
            {
                ESP_LOGE(TAG, "Failed to save WiFi credentials: %s", esp_err_to_name(err));
            }
            memset(s_pending_ssid, 0, sizeof(s_pending_ssid));
            memset(s_pending_pass, 0, sizeof(s_pending_pass));
        }
        else
        {
            ESP_LOGI(TAG, "connect != 1, ignored");
        }
    }

    return 0;
}

int wifi_config_service_init(void)
{
    memset(s_pending_ssid, 0, sizeof(s_pending_ssid));
    memset(s_pending_pass, 0, sizeof(s_pending_pass));
    return 0;
}

const struct ble_gatt_svc_def *wifi_config_service_get_svc_def(void)
{
    return wifi_svc_defs;
}

void wifi_config_service_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    (void)arg;
    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGI(TAG, "Registered WiFi svc, handle=%d", ctxt->svc.handle);
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGI(TAG, "Registered WiFi chr, def_handle=%d val_handle=%d",
                 ctxt->chr.def_handle, ctxt->chr.val_handle);
        break;
    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGI(TAG, "Registered WiFi dsc, handle=%d", ctxt->dsc.handle);
        break;
    default:
        break;
    }
}

void wifi_config_service_subscribe_cb(struct ble_gap_event *event)
{
    (void)event;
}
