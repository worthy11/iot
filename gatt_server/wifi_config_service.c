#include "wifi_config_service.h"
#include "common.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "../wifi_manager.h"
#include <string.h>

static const char *TAG = "WiFi_CFG_SVC";

static char s_pending_ssid[8] = {0};
static char s_pending_pass[16] = {0};

static const ble_uuid128_t WIFI_SVC_UUID   = BLE_UUID128_INIT(0xf0,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12);
static const ble_uuid128_t WIFI_SSID_UUID  = BLE_UUID128_INIT(0xf1,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12);
static const ble_uuid128_t WIFI_PASS_UUID  = BLE_UUID128_INIT(0xf2,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12);
static const ble_uuid128_t WIFI_APPLY_UUID = BLE_UUID128_INIT(0xf3,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12);

static int wifi_config_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def wifi_svc_defs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &WIFI_SVC_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &WIFI_SSID_UUID.u,
                .access_cb = wifi_config_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &WIFI_PASS_UUID.u,
                .access_cb = wifi_config_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {
                .uuid = &WIFI_APPLY_UUID.u,
                .access_cb = wifi_config_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {0},
        },
    },
    {0},
};

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

    if (len >= (int)sizeof(buf)) {
        len = sizeof(buf) - 1;
    }

    ble_hs_mbuf_to_flat(om, buf, len, NULL);
    buf[len] = '\0';

    if (ble_uuid_cmp(uuid, &WIFI_SSID_UUID.u) == 0) {
        size_t copy_len = len < (int)sizeof(s_pending_ssid) - 1 ? (size_t)len : sizeof(s_pending_ssid) - 1;
        memcpy(s_pending_ssid, buf, copy_len);
        s_pending_ssid[copy_len] = '\0';
        ESP_LOGI(TAG, "SSID set to '%s'", s_pending_ssid);
    } else if (ble_uuid_cmp(uuid, &WIFI_PASS_UUID.u) == 0) {
        size_t copy_len = len < (int)sizeof(s_pending_pass) - 1 ? (size_t)len : sizeof(s_pending_pass) - 1;
        memcpy(s_pending_pass, buf, copy_len);
        s_pending_pass[copy_len] = '\0';
        ESP_LOGI(TAG, "Password received (len=%d)", len);
    } else if (ble_uuid_cmp(uuid, &WIFI_APPLY_UUID.u) == 0) {
        if (len > 0 && buf[0] == 0x01) {
            ESP_LOGI(TAG, "APPLY=1 -> saving WiFi credentials");
            esp_err_t err = wifi_manager_save_credentials(s_pending_ssid, s_pending_pass);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "WiFi credentials saved successfully. Restart to apply.");
            }
        } else {
            ESP_LOGI(TAG, "APPLY != 1, ignored");
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
    switch (ctxt->op) {
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
