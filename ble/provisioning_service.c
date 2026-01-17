#include "provisioning_service.h"
#include "common.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "event_manager.h"
#include "utils/fs_utils.h"
#include "utils/nvs_utils.h"
#include "wifi_manager.h"
#include <string.h>

#define NVS_NAMESPACE "wifi"

static const char *TAG = "provisioning_service";

static const ble_uuid128_t PROVISIONING_SVC_UUID = BLE_UUID128_INIT(0xe0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_SSID_UUID = BLE_UUID128_INIT(0xe1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_PASSWORD_UUID = BLE_UUID128_INIT(0xe2, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_CERTIFICATE_UUID = BLE_UUID128_INIT(0xe3, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_PRIVATE_KEY_UUID = BLE_UUID128_INIT(0xe4, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_ROOT_CA_UUID = BLE_UUID128_INIT(0xe6, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_APPLY_UUID = BLE_UUID128_INIT(0xe5, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static char s_pending_ssid[32] = {0};
static char s_pending_password[64] = {0};
static char s_pending_certificate[2048] = {0};
static char s_pending_private_key[2048] = {0};
static char s_pending_root_ca[2048] = {0};

static size_t s_certificate_len = 0;
static size_t s_private_key_len = 0;
static size_t s_root_ca_len = 0;
static const ble_uuid_t *s_current_write_uuid = NULL;

static int provisioning_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static int provisioning_read_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg);
static int provisioning_desc_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def provisioning_svc_defs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &PROVISIONING_SVC_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &PROV_SSID_UUID.u,
                .access_cb = provisioning_write_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2901), .att_flags = BLE_ATT_F_READ, .min_key_size = 0, .access_cb = provisioning_desc_cb, .arg = "SSID"}, {0}},
            },
            {
                .uuid = &PROV_PASSWORD_UUID.u,
                .access_cb = provisioning_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2901), .att_flags = BLE_ATT_F_READ, .min_key_size = 0, .access_cb = provisioning_desc_cb, .arg = "Password"}, {0}},
            },
            {
                .uuid = &PROV_CERTIFICATE_UUID.u,
                .access_cb = provisioning_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2901), .att_flags = BLE_ATT_F_READ, .min_key_size = 0, .access_cb = provisioning_desc_cb, .arg = "Device Certificate"}, {0}},
            },
            {
                .uuid = &PROV_PRIVATE_KEY_UUID.u,
                .access_cb = provisioning_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2901), .att_flags = BLE_ATT_F_READ, .min_key_size = 0, .access_cb = provisioning_desc_cb, .arg = "Private Key"}, {0}},
            },
            {
                .uuid = &PROV_ROOT_CA_UUID.u,
                .access_cb = provisioning_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2901), .att_flags = BLE_ATT_F_READ, .min_key_size = 0, .access_cb = provisioning_desc_cb, .arg = "Root CA Certificate"}, {0}},
            },
            {
                .uuid = &PROV_APPLY_UUID.u,
                .access_cb = provisioning_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2901), .att_flags = BLE_ATT_F_READ, .min_key_size = 0, .access_cb = provisioning_desc_cb, .arg = "Apply (write to save all and trigger provisioning)"}, {0}},
            },
            {0},
        },
    },
    {0},
};

static void save_all_provisioning_data(void)
{
    ESP_LOGI(TAG, "Saving provisioning data...");

    if (s_pending_ssid[0] != '\0')
    {
        esp_err_t err = nvs_save_blob(NVS_NAMESPACE, "ssid", s_pending_ssid, strlen(s_pending_ssid));
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "Saved WiFi credentials: SSID=%s", s_pending_ssid);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to save WiFi credentials: %s", esp_err_to_name(err));
        }
    }

    if (s_certificate_len > 0)
    {
        s_pending_certificate[s_certificate_len] = '\0';
        fs_utils_save_device_certificate(s_pending_certificate, s_certificate_len);
        s_certificate_len = 0;
        memset(s_pending_certificate, 0, sizeof(s_pending_certificate));
    }

    if (s_private_key_len > 0)
    {
        s_pending_private_key[s_private_key_len] = '\0';
        fs_utils_save_private_key(s_pending_private_key, s_private_key_len);
        s_private_key_len = 0;
        memset(s_pending_private_key, 0, sizeof(s_pending_private_key));
    }

    if (s_root_ca_len > 0)
    {
        s_pending_root_ca[s_root_ca_len] = '\0';
        fs_utils_save_root_ca(s_pending_root_ca, s_root_ca_len);
        s_root_ca_len = 0;
        memset(s_pending_root_ca, 0, sizeof(s_pending_root_ca));
    }
}

static int provisioning_read_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    const ble_uuid_t *uuid = ctxt->chr->uuid;

    if (ble_uuid_cmp(uuid, &PROV_SSID_UUID.u) == 0)
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
            return os_mbuf_append(ctxt->om, ssid_to_return, strlen(ssid_to_return));
        }
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static int provisioning_desc_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const char *desc = (const char *)arg;
    return os_mbuf_append(ctxt->om, desc, strlen(desc));
}

static int provisioning_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    const ble_uuid_t *uuid = ctxt->chr->uuid;
    struct os_mbuf *om = ctxt->om;
    int len = OS_MBUF_PKTLEN(om);

    uint8_t buf[512];
    if (len >= (int)sizeof(buf))
    {
        len = sizeof(buf) - 1;
    }

    ble_hs_mbuf_to_flat(om, buf, len, NULL);

    if (ble_uuid_cmp(uuid, &PROV_SSID_UUID.u) == 0)
    {
        size_t copy_len = len < (int)sizeof(s_pending_ssid) - 1 ? (size_t)len : sizeof(s_pending_ssid) - 1;
        memcpy(s_pending_ssid, buf, copy_len);
        s_pending_ssid[copy_len] = '\0';
        ESP_LOGI(TAG, "SSID set to '%s' (pending)", s_pending_ssid);
    }
    else if (ble_uuid_cmp(uuid, &PROV_PASSWORD_UUID.u) == 0)
    {
        size_t copy_len = len < (int)sizeof(s_pending_password) - 1 ? (size_t)len : sizeof(s_pending_password) - 1;
        memcpy(s_pending_password, buf, copy_len);
        s_pending_password[copy_len] = '\0';
        ESP_LOGI(TAG, "Password received (pending)");
    }
    else if (ble_uuid_cmp(uuid, &PROV_CERTIFICATE_UUID.u) == 0)
    {
        if (s_current_write_uuid != NULL && s_current_write_uuid != uuid)
        {
            s_certificate_len = 0;
            memset(s_pending_certificate, 0, sizeof(s_pending_certificate));
            s_current_write_uuid = uuid;
        }
        else if (s_current_write_uuid == NULL)
        {
            s_certificate_len = 0;
            memset(s_pending_certificate, 0, sizeof(s_pending_certificate));
            s_current_write_uuid = uuid;
        }

        size_t remaining = sizeof(s_pending_certificate) - 1 - s_certificate_len;
        size_t copy_len = len < remaining ? (size_t)len : remaining;
        if (copy_len > 0)
        {
            ESP_LOGI(TAG, "received certificate chunk");
            memcpy(s_pending_certificate + s_certificate_len, buf, copy_len);
            s_certificate_len += copy_len;
            s_pending_certificate[s_certificate_len] = '\0';
        }
    }
    else if (ble_uuid_cmp(uuid, &PROV_PRIVATE_KEY_UUID.u) == 0)
    {
        if (s_current_write_uuid != NULL && s_current_write_uuid != uuid)
        {
            s_private_key_len = 0;
            memset(s_pending_private_key, 0, sizeof(s_pending_private_key));
            s_current_write_uuid = uuid;
        }
        else if (s_current_write_uuid == NULL)
        {
            s_private_key_len = 0;
            memset(s_pending_private_key, 0, sizeof(s_pending_private_key));
            s_current_write_uuid = uuid;
        }

        size_t remaining = sizeof(s_pending_private_key) - 1 - s_private_key_len;
        size_t copy_len = len < remaining ? (size_t)len : remaining;
        if (copy_len > 0)
        {
            ESP_LOGI(TAG, "received private_key chunk");
            memcpy(s_pending_private_key + s_private_key_len, buf, copy_len);
            s_private_key_len += copy_len;
            s_pending_private_key[s_private_key_len] = '\0';
        }
    }
    else if (ble_uuid_cmp(uuid, &PROV_ROOT_CA_UUID.u) == 0)
    {
        if (s_current_write_uuid != NULL && s_current_write_uuid != uuid)
        {
            s_root_ca_len = 0;
            memset(s_pending_root_ca, 0, sizeof(s_pending_root_ca));
            s_current_write_uuid = uuid;
        }
        else if (s_current_write_uuid == NULL)
        {
            s_root_ca_len = 0;
            memset(s_pending_root_ca, 0, sizeof(s_pending_root_ca));
            s_current_write_uuid = uuid;
        }

        size_t remaining = sizeof(s_pending_root_ca) - 1 - s_root_ca_len;
        size_t copy_len = len < remaining ? (size_t)len : remaining;
        if (copy_len > 0)
        {
            ESP_LOGI(TAG, "received root_ca chunk");
            memcpy(s_pending_root_ca + s_root_ca_len, buf, copy_len);
            s_root_ca_len += copy_len;
            s_pending_root_ca[s_root_ca_len] = '\0';
        }
    }
    else if (ble_uuid_cmp(uuid, &PROV_APPLY_UUID.u) == 0)
    {
        save_all_provisioning_data();
        s_current_write_uuid = NULL;
        event_manager_set_bits(EVENT_BIT_PROVISIONING_CHANGED);
    }

    return 0;
}

const struct ble_gatt_svc_def *provisioning_service_get_svc_def(void)
{
    return provisioning_svc_defs;
}
