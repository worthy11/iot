#include "gap.h"
#include "common.h"
#include "gatt_svc.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/ble_att.h"
#include "store/config/ble_store_config.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "esp_random.h"

#include "event_manager.h"

#define PREFERRED_MTU 512 // Request larger MTU for faster data transfer

static const char *TAG = "GAP";

inline static void format_addr(char *addr_str, uint8_t addr[]);
static void start_advertising(void);
static int gap_event_handler(struct ble_gap_event *event, void *arg);

static uint8_t own_addr_type;
static uint8_t addr_val[6] = {0};
static uint32_t current_passkey = 0;
static uint16_t passkey_conn_handle = BLE_HS_CONN_HANDLE_NONE;

uint32_t gap_get_current_passkey(void)
{
    return current_passkey;
}

inline static void format_addr(char *addr_str, uint8_t addr[])
{
    sprintf(addr_str, "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1],
            addr[2], addr[3], addr[4], addr[5]);
}

static uint32_t generate_passkey(void)
{
    return (uint32_t)(esp_random() % 1000000);
}

static int ble_gap_passkey_action_cb(struct ble_gap_event *event, void *arg)
{
    int rc = 0;
    struct ble_gap_conn_desc desc;

    switch (event->passkey.params.action)
    {
    case BLE_SM_IOACT_DISP:
        // Check if device is already bonded
        rc = ble_gap_conn_find(event->passkey.conn_handle, &desc);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "Failed to find connection for passkey: %d", rc);
            return rc;
        }

        // Only display passkey if device is not bonded
        if (!desc.sec_state.bonded)
        {
            current_passkey = generate_passkey();
            passkey_conn_handle = event->passkey.conn_handle;

            ESP_LOGI(TAG, "Passkey: %06lu", (unsigned long)current_passkey);

            event_manager_set_bits(EVENT_BIT_PASSKEY_DISPLAY);
            struct ble_sm_io io_data = {
                .action = BLE_SM_IOACT_DISP,
                .passkey = current_passkey};
            rc = ble_sm_inject_io(event->passkey.conn_handle, &io_data);
            if (rc != 0)
            {
                ESP_LOGE(TAG, "Failed to inject passkey: %d (0x%04x)", rc, rc);
            }
        }
        else
        {
            ESP_LOGI(TAG, "Device is bonded, skipping passkey display");
            // Still need to inject IO, but without displaying passkey
            struct ble_sm_io io_data = {
                .action = BLE_SM_IOACT_NONE};
            rc = ble_sm_inject_io(event->passkey.conn_handle, &io_data);
            if (rc != 0)
            {
                ESP_LOGE(TAG, "Failed to inject IO: %d (0x%04x)", rc, rc);
            }
        }
        break;
    }

    return rc;
}

static void start_advertising(void)
{
    int rc = 0;
    const char *name;
    struct ble_hs_adv_fields adv_fields = {0};
    struct ble_hs_adv_fields rsp_fields = {0};
    struct ble_gap_adv_params adv_params = {0};

    static const ble_uuid16_t adv_uuids16[] = {
        BLE_UUID16_INIT(0x180F)};

    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    adv_fields.appearance = 0x0000;
    adv_fields.appearance_is_present = 1;

    name = ble_svc_gap_device_name();
    adv_fields.name = (uint8_t *)name;
    adv_fields.name_len = strlen(name);
    adv_fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to set advertising data, error code: %d", rc);
        return;
    }

    rsp_fields.uuids16 = adv_uuids16;
    rsp_fields.num_uuids16 = sizeof(adv_uuids16) / sizeof(adv_uuids16[0]);
    rsp_fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to set scan response data, error code: %d", rc);
        return;
    }

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(500);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(510);

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           gap_event_handler, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to start advertising, error code: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "advertising started!");
}

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    int rc = 0;
    struct ble_gap_conn_desc desc;

    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc != 0)
            {
                ESP_LOGE(TAG,
                         "failed to find connection by handle, error code: %d",
                         rc);
                return rc;
            }

            // Set preferred MTU for larger data chunks
            rc = ble_att_set_preferred_mtu(PREFERRED_MTU);
            if (rc != 0)
            {
                ESP_LOGW(TAG, "Failed to set preferred MTU: %d (will use default)", rc);
            }
            else
            {
                ESP_LOGI(TAG, "Preferred MTU set to %d", PREFERRED_MTU);
            }

            ESP_LOGI(TAG, "Connection security: encrypted=%d, authenticated=%d, bonded=%d",
                     desc.sec_state.encrypted, desc.sec_state.authenticated, desc.sec_state.bonded);

            if (desc.sec_state.bonded)
            {
                ESP_LOGI(TAG, "Device is already bonded, using existing bond");
                event_manager_set_bits(EVENT_BIT_BLE_CONNECTED);
            }
            else
            {
                ESP_LOGI(TAG, "Device not bonded");
                if (event_manager_get_bits() & EVENT_BIT_PAIRING_MODE)
                {
                    ESP_LOGI(TAG, "In pairing mode, initiating pairing/encryption...");
                    rc = ble_gap_security_initiate(event->connect.conn_handle);
                    if (rc != 0)
                    {
                        ESP_LOGE(TAG, "Failed to initiate security: %d", rc);
                    }
                }
                else
                {
                    ESP_LOGI(TAG, "Not in pairing mode, rejecting unpaired device");
                    ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                }
            }

            struct ble_gap_upd_params params = {.itvl_min = desc.conn_itvl,
                                                .itvl_max = desc.conn_itvl,
                                                .latency = 3,
                                                .supervision_timeout =
                                                    desc.supervision_timeout};
            rc = ble_gap_update_params(event->connect.conn_handle, &params);
            if (rc != 0)
            {
                ESP_LOGE(
                    TAG,
                    "failed to update connection parameters, error code: %d",
                    rc);
                return rc;
            }

            if (passkey_conn_handle == event->connect.conn_handle)
            {
                event_manager_clear_bits(EVENT_BIT_PASSKEY_DISPLAY);
                passkey_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                current_passkey = 0;
            }
        }
        else
        {
            ESP_LOGW(TAG, "Connection failed with status: %d", event->connect.status);
        }
        return rc;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        rc = ble_gap_passkey_action_cb(event, NULL);
        return rc;

    case BLE_GAP_EVENT_ENC_CHANGE:
        struct ble_gap_conn_desc desc;
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        if (rc == 0)
        {
            if (event->enc_change.status == 0 && desc.sec_state.encrypted)
            {
                ESP_LOGI(TAG, "Connection successfully encrypted and authenticated");
                if (passkey_conn_handle == event->enc_change.conn_handle)
                {
                    event_manager_clear_bits(EVENT_BIT_PASSKEY_DISPLAY);
                    passkey_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                    current_passkey = 0;
                }
                // If in pairing mode and encryption successful, device is now paired
                if (event_manager_get_bits() & EVENT_BIT_PAIRING_MODE)
                {
                    // Device successfully paired - set connected bit so pairing task can continue
                    ESP_LOGI(TAG, "Device successfully paired in pairing mode");
                    event_manager_set_bits(EVENT_BIT_PAIRING_SUCCESS);
                }
            }
            else if (event->enc_change.status != 0)
            {
                ESP_LOGE(TAG, "Encryption change failed with status: %d", event->enc_change.status);
            }
        }
        return rc;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected from peer; reason=%d (0x%02x)",
                 event->disconnect.reason, event->disconnect.reason);

        if (passkey_conn_handle != BLE_HS_CONN_HANDLE_NONE)
        {
            event_manager_clear_bits(EVENT_BIT_PASSKEY_DISPLAY);
            passkey_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            current_passkey = 0;
        }
        event_manager_set_bits(EVENT_BIT_BLE_DISCONNECTED);
        return rc;

    case BLE_GAP_EVENT_CONN_UPDATE:
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "failed to find connection by handle, error code: %d",
                     rc);
            return rc;
        }
        return rc;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        return rc;

    case BLE_GAP_EVENT_NOTIFY_TX:
        if ((event->notify_tx.status != 0) &&
            (event->notify_tx.status != BLE_HS_EDONE))
        {
            ESP_LOGI(TAG,
                     "Notify event; conn_handle=%d attr_handle=%d "
                     "status=%d is_indication=%d",
                     event->notify_tx.conn_handle, event->notify_tx.attr_handle,
                     event->notify_tx.status, event->notify_tx.indication);
        }
        return rc;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU exchange complete: conn_handle=%d, mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        return rc;
    }

    return rc;
}

void adv_init(void)
{
    int rc = 0;
    char addr_str[18] = {0};

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "device does not have any available bt address!");
        return;
    }

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to infer address type, error code: %d", rc);
        return;
    }

    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to copy device address, error code: %d", rc);
        return;
    }
    format_addr(addr_str, addr_val);
    ESP_LOGI(TAG, "device address: %s", addr_str);

    start_advertising();
}

int gap_init(void)
{
    int rc = 0;

    ble_svc_gap_init();

    rc = ble_svc_gap_device_name_set(DEVICE_NAME);

    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY; // Display only - show passkey
    ble_hs_cfg.sm_bonding = 1;                      // Enable bonding
    ble_hs_cfg.sm_mitm = 1;                         // Require MITM protection (passkey)
    ble_hs_cfg.sm_sc = 0;                           // Legacy pairing (not secure connections)
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    return rc;
}
