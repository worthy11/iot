#include "device_provisioning_service.h"
#include "common.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "event_manager.h"
#include "utils/nvs_utils.h"
#include <string.h>

static const char *TAG = "device_provisioning";

// Provisioning service UUIDs
static const ble_uuid128_t PROVISIONING_SVC_UUID = BLE_UUID128_INIT(0xe0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_TOPIC_ID_UUID = BLE_UUID128_INIT(0xe1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_CERTIFICATE_UUID = BLE_UUID128_INIT(0xe2, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_PRIVATE_KEY_UUID = BLE_UUID128_INIT(0xe3, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_ROOT_CA_UUID = BLE_UUID128_INIT(0xe4, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);
static const ble_uuid128_t PROV_APPLY_UUID = BLE_UUID128_INIT(0xe5, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

// Pending provisioning data
static char s_pending_topic_id[64] = {0};
static char s_pending_certificate[2048] = {0};
static char s_pending_private_key[2048] = {0};
static char s_pending_root_ca[2048] = {0};

// Chunked write state
static size_t s_certificate_len = 0;
static size_t s_private_key_len = 0;
static size_t s_root_ca_len = 0;
static const ble_uuid_t *s_current_write_uuid = NULL;

static int provisioning_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg);
static int provisioning_desc_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def provisioning_svc_defs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &PROVISIONING_SVC_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &PROV_TOPIC_ID_UUID.u,
                .access_cb = provisioning_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2901), .att_flags = BLE_ATT_F_READ, .min_key_size = 0, .access_cb = provisioning_desc_cb, .arg = "Topic ID"}, {0}},
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
                .descriptors = (struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2901), .att_flags = BLE_ATT_F_READ, .min_key_size = 0, .access_cb = provisioning_desc_cb, .arg = "Root CA"}, {0}},
            },
            {
                .uuid = &PROV_APPLY_UUID.u,
                .access_cb = provisioning_write_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
                .min_key_size = 16,
                .descriptors = (struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2901), .att_flags = BLE_ATT_F_READ, .min_key_size = 0, .access_cb = provisioning_desc_cb, .arg = "Apply Provisioning"}, {0}},
            },
            {0},
        },
    },
    {0},
};

static void save_certificates(void)
{
    if (s_certificate_len > 0 && s_pending_certificate[0] != '\0')
    {
        ESP_LOGI(TAG, "Saving device certificate to NVS (%zu bytes)", s_certificate_len);
        nvs_save_device_certificate(s_pending_certificate);
        s_certificate_len = 0; // Reset after saving
    }

    // Save private key if we have data
    if (s_private_key_len > 0 && s_pending_private_key[0] != '\0')
    {
        ESP_LOGI(TAG, "Saving private key to NVS (%zu bytes)", s_private_key_len);
        nvs_save_private_key(s_pending_private_key);
        s_private_key_len = 0; // Reset after saving
    }

    // Save root CA if we have data
    if (s_root_ca_len > 0 && s_pending_root_ca[0] != '\0')
    {
        ESP_LOGI(TAG, "Saving root CA to NVS (%zu bytes)", s_root_ca_len);
        nvs_save_root_ca(s_pending_root_ca);
        s_root_ca_len = 0; // Reset after saving
    }
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
    uint8_t buf[512]; // Buffer for chunk (MTU is 256, so this is safe)

    if (len > (int)sizeof(buf))
    {
        len = sizeof(buf);
    }

    ble_hs_mbuf_to_flat(om, buf, len, NULL);

    if (ble_uuid_cmp(uuid, &PROV_TOPIC_ID_UUID.u) == 0)
    {
        size_t copy_len = len < (int)sizeof(s_pending_topic_id) - 1 ? (size_t)len : sizeof(s_pending_topic_id) - 1;
        memcpy(s_pending_topic_id, buf, copy_len);
        s_pending_topic_id[copy_len] = '\0';
        ESP_LOGI(TAG, "Topic ID received: %s", s_pending_topic_id);
        nvs_save_topic_id(s_pending_topic_id);
        return 0;
    }
    else if (ble_uuid_cmp(uuid, &PROV_CERTIFICATE_UUID.u) == 0)
    {
        // If switching from another characteristic, just switch tracking
        if (s_current_write_uuid != NULL && s_current_write_uuid != uuid)
        {
            s_current_write_uuid = uuid;
        }
        // If this is a new write of certificate (was NULL or same UUID but we reset), reset
        else if (s_current_write_uuid == NULL)
        {
            s_certificate_len = 0;
            memset(s_pending_certificate, 0, sizeof(s_pending_certificate));
            s_current_write_uuid = uuid;
        }
        // Otherwise, continuation of same write (s_current_write_uuid == uuid)

        // Append chunk
        size_t remaining = sizeof(s_pending_certificate) - 1 - s_certificate_len;
        size_t copy_len = len < remaining ? (size_t)len : remaining;
        if (copy_len > 0)
        {
            memcpy(s_pending_certificate + s_certificate_len, buf, copy_len);
            s_certificate_len += copy_len;
            s_pending_certificate[s_certificate_len] = '\0';
            ESP_LOGI(TAG, "Certificate chunk received: %zu bytes (total: %zu)", copy_len, s_certificate_len);
        }
    }
    else if (ble_uuid_cmp(uuid, &PROV_PRIVATE_KEY_UUID.u) == 0)
    {
        // If switching from another characteristic, just switch tracking
        if (s_current_write_uuid != NULL && s_current_write_uuid != uuid)
        {
            s_current_write_uuid = uuid;
        }
        // If this is a new write of private key, reset
        else if (s_current_write_uuid == NULL)
        {
            s_private_key_len = 0;
            memset(s_pending_private_key, 0, sizeof(s_pending_private_key));
            s_current_write_uuid = uuid;
        }
        // Otherwise, continuation of same write

        size_t remaining = sizeof(s_pending_private_key) - 1 - s_private_key_len;
        size_t copy_len = len < remaining ? (size_t)len : remaining;
        if (copy_len > 0)
        {
            memcpy(s_pending_private_key + s_private_key_len, buf, copy_len);
            s_private_key_len += copy_len;
            s_pending_private_key[s_private_key_len] = '\0';
            ESP_LOGI(TAG, "Private key chunk received: %zu bytes (total: %zu)", copy_len, s_private_key_len);
        }
    }
    else if (ble_uuid_cmp(uuid, &PROV_ROOT_CA_UUID.u) == 0)
    {
        // If switching from another characteristic, just switch tracking
        if (s_current_write_uuid != NULL && s_current_write_uuid != uuid)
        {
            s_current_write_uuid = uuid;
        }
        // If this is a new write of root CA, reset
        else if (s_current_write_uuid == NULL)
        {
            s_root_ca_len = 0;
            memset(s_pending_root_ca, 0, sizeof(s_pending_root_ca));
            s_current_write_uuid = uuid;
        }
        // Otherwise, continuation of same write

        size_t remaining = sizeof(s_pending_root_ca) - 1 - s_root_ca_len;
        size_t copy_len = len < remaining ? (size_t)len : remaining;
        if (copy_len > 0)
        {
            memcpy(s_pending_root_ca + s_root_ca_len, buf, copy_len);
            s_root_ca_len += copy_len;
            s_pending_root_ca[s_root_ca_len] = '\0';
            ESP_LOGI(TAG, "Root CA chunk received: %zu bytes (total: %zu)", copy_len, s_root_ca_len);
        }
    }
    else if (ble_uuid_cmp(uuid, &PROV_APPLY_UUID.u) == 0)
    {
        ESP_LOGI(TAG, "Apply provisioning characteristic written, triggering provisioning");
        save_certificates();
        event_manager_set_bits(EVENT_BIT_PROVISION_TRIGGER);
        return 0;
    }

    return 0;
}

const struct ble_gatt_svc_def *device_provisioning_service_get_svc_def(void)
{
    return provisioning_svc_defs;
}