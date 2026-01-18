#include "provisioning_service.h"
#include "common.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_store.h"
#include "host/ble_gap.h"
#include "store/config/ble_store_config.h"
#include "event_manager.h"
#include "utils/fs_utils.h"
#include "utils/nvs_utils.h"
#include "wifi_manager.h"
#include <string.h>

#define WIFI_CONFIG_NAMESPACE "wifi_cfg"

static const char *TAG = "provisioning_service";

static const ble_uuid128_t PROVISIONING_SVC_UUID = BLE_UUID128_INIT(0xe0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_SSID_UUID = BLE_UUID128_INIT(0xe1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_PASSWORD_UUID = BLE_UUID128_INIT(0xe2, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_CERTIFICATE_UUID = BLE_UUID128_INIT(0xe3, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_PRIVATE_KEY_UUID = BLE_UUID128_INIT(0xe4, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_ROOT_CA_UUID = BLE_UUID128_INIT(0xe5, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_MAC_UUID = BLE_UUID128_INIT(0xe6, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_FORGET_DEVICE_UUID = BLE_UUID128_INIT(0xe7, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_APPLY_UUID = BLE_UUID128_INIT(0xe8, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

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
                .uuid = &PROV_MAC_UUID.u,
                .access_cb = provisioning_read_cb,
                .flags = BLE_GATT_CHR_F_READ,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2901), .att_flags = BLE_ATT_F_READ, .min_key_size = 0, .access_cb = provisioning_desc_cb, .arg = "Device MAC Address"}, {0}},
            },
            {
                .uuid = &PROV_APPLY_UUID.u,
                .access_cb = provisioning_write_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2901), .att_flags = BLE_ATT_F_READ, .min_key_size = 0, .access_cb = provisioning_desc_cb, .arg = "Apply (write to save all and trigger provisioning)"}, {0}},
            },
            {
                .uuid = &PROV_FORGET_DEVICE_UUID.u,
                .access_cb = provisioning_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2901), .att_flags = BLE_ATT_F_READ, .min_key_size = 0, .access_cb = provisioning_desc_cb, .arg = "Forget Device (write 1 to remove bond)"}, {0}},
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
        esp_err_t err = nvs_save_blob(WIFI_CONFIG_NAMESPACE, "ssid", s_pending_ssid, strlen(s_pending_ssid));
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "Saved WiFi SSID: %s", s_pending_ssid);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to save WiFi SSID: %s", esp_err_to_name(err));
        }

        if (s_pending_password[0] != '\0')
        {
            err = nvs_save_blob(WIFI_CONFIG_NAMESPACE, "pass", s_pending_password, strlen(s_pending_password));
            if (err == ESP_OK)
            {
                ESP_LOGI(TAG, "Saved WiFi password");
            }
            else
            {
                ESP_LOGE(TAG, "Failed to save WiFi password: %s", esp_err_to_name(err));
            }
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

    // Save device MAC address as client ID
    uint8_t base_mac[6] = {0};
    esp_err_t err = esp_read_mac(base_mac, ESP_MAC_BASE);
    if (err == ESP_OK)
    {
        char mac_str[13]; // 12 hex digits + null terminator (no colons)
        snprintf(mac_str, sizeof(mac_str), "%02X%02X%02X%02X%02X%02X",
                 base_mac[0], base_mac[1], base_mac[2],
                 base_mac[3], base_mac[4], base_mac[5]);

        err = fs_utils_save_client_id(mac_str);
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "Saved device MAC as client ID: %s", mac_str);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to save client ID: %s", esp_err_to_name(err));
        }
    }
    else
    {
        ESP_LOGE(TAG, "Failed to read base MAC address for client ID: %s", esp_err_to_name(err));
    }
}

static int provisioning_read_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)arg;

    const ble_uuid_t *uuid = ctxt->chr->uuid;

    ESP_LOGI(TAG, "Provisioning read: conn_handle=%d, attr_handle=%d", conn_handle, attr_handle);

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
            ESP_LOGI(TAG, "Reading SSID: '%s'", ssid_to_return);
            return os_mbuf_append(ctxt->om, ssid_to_return, strlen(ssid_to_return));
        }
        ESP_LOGW(TAG, "SSID read requested but no SSID available");
        return 0;
    }
    else if (ble_uuid_cmp(uuid, &PROV_MAC_UUID.u) == 0)
    {
        uint8_t base_mac[6] = {0};
        esp_err_t err = esp_read_mac(base_mac, ESP_MAC_BASE);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to read base MAC address: %s", esp_err_to_name(err));
            return BLE_ATT_ERR_UNLIKELY;
        }

        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X%02X%02X%02X%02X%02X",
                 base_mac[0], base_mac[1], base_mac[2],
                 base_mac[3], base_mac[4], base_mac[5]);

        ESP_LOGI(TAG, "Reading embedded MAC address: %s", mac_str);
        return os_mbuf_append(ctxt->om, mac_str, strlen(mac_str));
    }
    else if (ble_uuid_cmp(uuid, &PROV_APPLY_UUID.u) == 0)
    {
        // Return status: 0 = ready, 1 = applied
        uint8_t status = 0;
        ESP_LOGI(TAG, "Reading Apply characteristic status: %d", status);
        return os_mbuf_append(ctxt->om, &status, sizeof(status));
    }

    ESP_LOGW(TAG, "Unknown characteristic read request");
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
    (void)arg;

    const ble_uuid_t *uuid = ctxt->chr->uuid;
    struct os_mbuf *om = ctxt->om;
    int len = OS_MBUF_PKTLEN(om);

    ESP_LOGI(TAG, "Provisioning write: conn_handle=%d, attr_handle=%d, len=%d", conn_handle, attr_handle, len);

    uint8_t buf[512];
    if (len >= (int)sizeof(buf))
    {
        len = sizeof(buf) - 1;
        ESP_LOGW(TAG, "Write data truncated to %d bytes", len);
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
        ESP_LOGI(TAG, "Apply characteristic written - saving all provisioning data");
        save_all_provisioning_data();
        s_current_write_uuid = NULL;
        event_manager_set_bits(EVENT_BIT_PROVISIONING_CHANGED);
        ESP_LOGI(TAG, "Provisioning data saved and change event triggered");
    }
    else if (ble_uuid_cmp(uuid, &PROV_FORGET_DEVICE_UUID.u) == 0)
    {
        // Forget/unbond the connected device
        uint8_t value = 0;
        if (len > 0)
        {
            value = buf[0];
        }

        if (value == 1)
        {
            ESP_LOGI(TAG, "Forgetting/unbonding connected device");

            // Get connection descriptor to find peer address
            struct ble_gap_conn_desc desc;
            int rc = ble_gap_conn_find(conn_handle, &desc);
            if (rc == 0)
            {
                // Validate that peer_id_addr is valid before using it
                // Address type should be a valid BLE address type (0-3)
                bool addr_valid = (desc.peer_id_addr.type <= BLE_ADDR_RANDOM_ID);

                if (addr_valid)
                {
                    // Delete all peer information (bonds, security keys, CCCDs, etc.)
                    // This function deletes ALL store entries for the peer
                    rc = ble_store_util_delete_peer(&desc.peer_id_addr);
                    if (rc == 0)
                    {
                        ESP_LOGI(TAG, "Successfully removed all peer data (bond, keys, CCCDs)");
                    }
                    else if (rc == BLE_HS_ENOENT)
                    {
                        ESP_LOGW(TAG, "No peer data found (rc=%d)", rc);
                    }
                    else
                    {
                        ESP_LOGE(TAG, "Failed to remove peer data: %d", rc);
                    }
                }
                else
                {
                    ESP_LOGW(TAG, "Invalid peer address type: %d, cannot delete bond", desc.peer_id_addr.type);
                }

                // Disconnect the device
                ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
            else
            {
                ESP_LOGE(TAG, "Failed to find connection: %d", rc);
            }
        }
        else
        {
            ESP_LOGW(TAG, "Forget Device write with value=%d (expected 1)", value);
        }
    }
    else
    {
        ESP_LOGW(TAG, "Unknown provisioning characteristic write request");
    }

    return 0;
}

const struct ble_gatt_svc_def *provisioning_service_get_svc_def(void)
{
    return provisioning_svc_defs;
}
