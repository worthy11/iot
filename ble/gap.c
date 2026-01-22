#include "gap.h"
#include "common.h"
#include "gatt_svc.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/ble_att.h"
#include "store/config/ble_store_config.h"
#include "esp_mac.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "esp_random.h"

#include "event_manager.h"
#include "hardware/hardware_manager.h"
#include "host/ble_sm.h"

#define PREFERRED_MTU 512 // Request larger MTU for faster data transfer

static const char *TAG = "GAP";

inline static void format_addr(char *addr_str, uint8_t addr[]);
static void start_advertising(void);
static int gap_event_handler(struct ble_gap_event *event, void *arg);

static uint8_t own_addr_type;
static uint8_t addr_val[6] = {0};

inline static void format_addr(char *addr_str, uint8_t addr[])
{
    sprintf(addr_str, "%02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1],
            addr[2], addr[3], addr[4], addr[5]);
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
            event_manager_set_bits(EVENT_BIT_BLE_CONNECTED);

            ESP_LOGI(TAG, "Connection security: encrypted=%d, authenticated=%d, bonded=%d",
                     desc.sec_state.encrypted, desc.sec_state.authenticated, desc.sec_state.bonded);

            EventBits_t bits = event_manager_get_bits();
            bool pairing_mode_active = (bits & EVENT_BIT_PAIRING_MODE_ON) != 0;
            ESP_LOGI(TAG, "Pairing mode status: %s", pairing_mode_active ? "ON" : "OFF");

            if (!desc.sec_state.encrypted || !desc.sec_state.authenticated)
            {
                if (pairing_mode_active)
                {
                    // Pairing mode is ON - allow new devices to pair
                    ESP_LOGI(TAG, "Device connected in pairing mode - initiating security");
                    rc = ble_gap_security_initiate(event->connect.conn_handle);
                    if (rc != 0)
                    {
                        ESP_LOGE(TAG, "Failed to initiate security: %d", rc);
                        ESP_LOGW(TAG, "Security initiation failed - rejecting connection");
                        rc = ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                        if (rc != 0)
                        {
                            ESP_LOGE(TAG, "Failed to terminate connection: %d", rc);
                        }
                        event_manager_clear_bits(EVENT_BIT_BLE_CONNECTED);
                        event_manager_set_bits(EVENT_BIT_BLE_DISCONNECTED);
                        return 0;
                    }
                }
                else
                {
                    // Pairing mode is OFF - check if bond exists in store before initiating security
                    // If we initiate security without checking, it will allow pairing even if mode is off
                    // Use ble_store_read_peer_sec to check if security keys exist for this peer
                    ESP_LOGI(TAG, "Pairing mode OFF - checking for existing bond in store");
                    ESP_LOGI(TAG, "Peer address: type=%d, val=%02x:%02x:%02x:%02x:%02x:%02x",
                             desc.peer_id_addr.type,
                             desc.peer_id_addr.val[0], desc.peer_id_addr.val[1],
                             desc.peer_id_addr.val[2], desc.peer_id_addr.val[3],
                             desc.peer_id_addr.val[4], desc.peer_id_addr.val[5]);

                    struct ble_store_key_sec key_sec;
                    memset(&key_sec, 0, sizeof(key_sec));
                    key_sec.peer_addr = desc.peer_id_addr;

                    struct ble_store_value_sec value_sec;
                    memset(&value_sec, 0, sizeof(value_sec));
                    rc = ble_store_read_peer_sec(&key_sec, &value_sec);

                    if (rc == 0)
                    {
                        // Bond exists in store - initiate security to restore encryption
                        ESP_LOGI(TAG, "Bond found in store for this device - initiating security to restore encryption");
                        rc = ble_gap_security_initiate(event->connect.conn_handle);
                        if (rc != 0)
                        {
                            ESP_LOGE(TAG, "Failed to initiate security: %d", rc);
                            ESP_LOGW(TAG, "Security initiation failed - rejecting connection");
                            rc = ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                            if (rc != 0)
                            {
                                ESP_LOGE(TAG, "Failed to terminate connection: %d", rc);
                            }
                            event_manager_clear_bits(EVENT_BIT_BLE_CONNECTED);
                            event_manager_set_bits(EVENT_BIT_BLE_DISCONNECTED);
                            return 0;
                        }
                    }
                    else
                    {
                        // No bond found - reject immediately without initiating security
                        // This prevents unbonded devices from pairing when pairing mode is off
                        ESP_LOGW(TAG, "No bond found in store (rc=%d) and pairing mode is OFF - rejecting connection", rc);
                        ESP_LOGW(TAG, "To pair a new device, enable pairing mode from the display menu first");
                        rc = ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                        if (rc != 0)
                        {
                            ESP_LOGE(TAG, "Failed to terminate connection: %d", rc);
                        }
                        event_manager_clear_bits(EVENT_BIT_BLE_CONNECTED);
                        event_manager_set_bits(EVENT_BIT_BLE_DISCONNECTED);
                        return 0;
                    }
                }
            }
            else
            {
                if (desc.sec_state.bonded)
                {
                    ESP_LOGI(TAG, "Bonded device connected with existing encryption");
                }
                else if (!pairing_mode_active)
                {
                    ESP_LOGW(TAG, "Unbonded encrypted device but pairing mode is OFF - rejecting");
                    rc = ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                    if (rc != 0)
                    {
                        ESP_LOGE(TAG, "Failed to terminate connection: %d", rc);
                    }
                    event_manager_clear_bits(EVENT_BIT_BLE_CONNECTED);
                    event_manager_set_bits(EVENT_BIT_BLE_DISCONNECTED);
                    return 0;
                }
                else
                {
                    ESP_LOGI(TAG, "Unbonded device connected in pairing mode - accepting");
                }
            }

            rc = ble_att_set_preferred_mtu(PREFERRED_MTU);
            if (rc != 0)
            {
                ESP_LOGW(TAG, "Failed to set preferred MTU: %d (will use default)", rc);
            }
            else
            {
                ESP_LOGI(TAG, "Preferred MTU set to %d", PREFERRED_MTU);
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
        }
        else
        {
            ESP_LOGW(TAG, "Connection failed with status: %d", event->connect.status);
        }
        return rc;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "BLE_GAP_EVENT_ENC_CHANGE received: conn_handle=%d, status=%d",
                 event->enc_change.conn_handle, event->enc_change.status);
        struct ble_gap_conn_desc desc;
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        if (rc == 0)
        {
            ESP_LOGI(TAG, "Connection desc: encrypted=%d, authenticated=%d, bonded=%d",
                     desc.sec_state.encrypted, desc.sec_state.authenticated, desc.sec_state.bonded);
            if (event->enc_change.status == 0 && desc.sec_state.encrypted)
            {
                ESP_LOGI(TAG, "Connection successfully encrypted and authenticated");
                ESP_LOGI(TAG, "Security state: encrypted=%d, authenticated=%d, bonded=%d",
                         desc.sec_state.encrypted, desc.sec_state.authenticated, desc.sec_state.bonded);

                bool is_bonded = desc.sec_state.bonded;

                EventBits_t bits = event_manager_get_bits();
                bool pairing_mode_active = (bits & EVENT_BIT_PAIRING_MODE_ON) != 0;

                if (is_bonded)
                {
                    // Bonded device - encryption restored from stored keys
                    ESP_LOGI(TAG, "Bonded device connected - encryption restored from stored keys");
                    ESP_LOGI(TAG, "Bond information is saved and encryption restored successfully");
                }
                else
                {
                    // Unbonded device - check pairing mode
                    if (!pairing_mode_active)
                    {
                        ESP_LOGW(TAG, "Unbonded device encrypted but pairing mode is OFF - rejecting");
                        ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_CONN_TERM_LOCAL);
                        event_manager_clear_bits(EVENT_BIT_BLE_CONNECTED);
                        event_manager_set_bits(EVENT_BIT_BLE_DISCONNECTED);
                        return 0;
                    }
                    else
                    {
                        ESP_LOGI(TAG, "New unbonded device successfully paired in pairing mode");
                        // Bonding should be saved automatically by NimBLE store via ble_store_util_status_rr callback
                        // The bond is saved when key distribution completes during pairing
                        // Check bond status multiple times as it may be saved asynchronously
                        for (int i = 0; i < 10; i++)
                        {
                            vTaskDelay(pdMS_TO_TICKS(200));
                            rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
                            if (rc == 0)
                            {
                                ESP_LOGI(TAG, "Check %d: encrypted=%d, authenticated=%d, bonded=%d",
                                         i + 1, desc.sec_state.encrypted, desc.sec_state.authenticated, desc.sec_state.bonded);
                                if (desc.sec_state.bonded)
                                {
                                    ESP_LOGI(TAG, "Bond successfully established and saved to store!");
                                    break;
                                }
                            }
                        }
                        if (!desc.sec_state.bonded)
                        {
                            ESP_LOGW(TAG, "Bond was not saved after pairing - this may indicate a store issue");
                            ESP_LOGW(TAG, "Device will need to pair again on next connection");
                        }
                    }
                }
            }
            else if (event->enc_change.status != 0)
            {
                const char *error_desc = "Unknown error";
                switch (event->enc_change.status)
                {
                case 7:
                    error_desc = "Invalid parameters";
                    break;
                case 13:
                    error_desc = "Operation preempted/cancelled (client may have cancelled pairing)";
                    break;
                case 1028:
                    error_desc = "Message size error";
                    break;
                case 1292:
                    error_desc = "Unknown error";
                    break;
                }
                ESP_LOGE(TAG, "Encryption change failed with status: %d (%s)", event->enc_change.status, error_desc);

                // Check if pairing mode is active - allow connection to continue even if encryption fails
                EventBits_t bits = event_manager_get_bits();
                bool pairing_mode_active = (bits & EVENT_BIT_PAIRING_MODE_ON) != 0;

                if (pairing_mode_active)
                {
                    ESP_LOGW(TAG, "Encryption failed but pairing mode is active - allowing connection to continue for provisioning");
                    // Don't terminate - allow provisioning even without encryption in pairing mode
                    // This is intentional: some clients may cancel pairing but still want to provision
                }
                else
                {
                    // Outside pairing mode, terminate connection if encryption fails
                    ESP_LOGW(TAG, "Encryption failed and pairing mode is OFF - terminating connection");
                    rc = ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                    if (rc != 0)
                    {
                        ESP_LOGE(TAG, "Failed to terminate connection: %d", rc);
                    }
                    event_manager_clear_bits(EVENT_BIT_BLE_CONNECTED);
                    event_manager_set_bits(EVENT_BIT_BLE_DISCONNECTED);
                }
            }
        }
        event_manager_clear_bits(EVENT_BIT_PAIRING_MODE_ON);
        event_manager_set_bits(EVENT_BIT_PAIRING_MODE_OFF);
        return rc;


    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected from peer; reason=%d (0x%02x)",
                 event->disconnect.reason, event->disconnect.reason);
        event_manager_clear_bits(EVENT_BIT_BLE_CONNECTED);
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
    uint8_t base_mac[6] = {0};

    // Get the embedded MAC address from eFuse (base MAC)
    esp_err_t err = esp_read_mac(base_mac, ESP_MAC_BASE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read base MAC address: %s", esp_err_to_name(err));
        // Fallback to BLE address
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
        ESP_LOGI(TAG, "device address (BLE): %s", addr_str);
    }
    else
    {
        // Use the embedded MAC address
        memcpy(addr_val, base_mac, 6);
        format_addr(addr_str, base_mac);
        ESP_LOGI(TAG, "device address (embedded MAC): %s", addr_str);
    }

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

    start_advertising();
}

static int ble_sm_io_cb(uint32_t passkey, void *arg)
{
    (void)arg;

    // Display passkey on the display
    char passkey_str[32];
    snprintf(passkey_str, sizeof(passkey_str), "%06lu", (unsigned long)passkey);
    ESP_LOGI(TAG, "Passkey for pairing: %s", passkey_str);

    // Display passkey on hardware display
    hardware_manager_display_event("passkey", (double)passkey);

    return 0;
}

void gap_configure_security(void)
{
    // Enable bonding with passkey entry
    // Device displays passkey that user must enter on their device
    ble_hs_cfg.sm_io_cap = 0;  // BLE_SM_IO_CAP_DISPLAY_ONLY (value 0) - Display passkey
    ble_hs_cfg.sm_bonding = 1; // Enable bonding - save bonded devices
    ble_hs_cfg.sm_mitm = 1;    // Enable MITM protection (requires passkey)
    ble_hs_cfg.sm_sc = 0;      // Legacy pairing (not secure connections)
    // Enable key distribution for bonding
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    
    // Note: In NimBLE ESP-IDF, the passkey callback (ble_sm_io_cb) is automatically
    // called by the stack when sm_io_cap is set to DISPLAY_ONLY and pairing requires a passkey.
    // The callback function is defined above and will be invoked when needed.
}

int gap_init(void)
{
    int rc = 0;

    ble_svc_gap_init();

    rc = ble_svc_gap_device_name_set(DEVICE_NAME);

    // Configure security based on pairing mode
    gap_configure_security();
    
    // Register passkey callback using ble_sm_set_io_cap
    // This must be called after gap_configure_security() sets sm_io_cap
    rc = ble_sm_set_io_cap(0, ble_sm_io_cb, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to set SM IO capability callback: %d", rc);
    }
    else
    {
        ESP_LOGI(TAG, "Passkey display callback registered");
    }

    return rc;
}
