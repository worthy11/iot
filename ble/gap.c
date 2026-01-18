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

    ESP_LOGI(TAG, "Passkey action callback: conn_handle=%d, action=%d",
             event->passkey.conn_handle, event->passkey.params.action);

    switch (event->passkey.params.action)
    {
    case BLE_SM_IOACT_DISP:
        ESP_LOGI(TAG, "Passkey display requested (BLE_SM_IOACT_DISP)");
        // Check if device is already bonded
        rc = ble_gap_conn_find(event->passkey.conn_handle, &desc);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "Failed to find connection for passkey: %d", rc);
            return rc;
        }

        ESP_LOGI(TAG, "Connection security state: encrypted=%d, authenticated=%d, bonded=%d",
                 desc.sec_state.encrypted, desc.sec_state.authenticated, desc.sec_state.bonded);

        // Only display passkey if device is not bonded
        if (!desc.sec_state.bonded)
        {
            current_passkey = generate_passkey();
            passkey_conn_handle = event->passkey.conn_handle;

            ESP_LOGI(TAG, "Generated passkey: %06lu", (unsigned long)current_passkey);

            event_manager_set_bits(EVENT_BIT_PASSKEY_DISPLAY);
            struct ble_sm_io io_data = {
                .action = BLE_SM_IOACT_DISP,
                .passkey = current_passkey};
            rc = ble_sm_inject_io(event->passkey.conn_handle, &io_data);
            if (rc != 0)
            {
                ESP_LOGE(TAG, "Failed to inject passkey: %d (0x%04x)", rc, rc);
            }
            else
            {
                ESP_LOGI(TAG, "Passkey injected successfully, should be displayed");
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

    case BLE_SM_IOACT_NONE:
        ESP_LOGI(TAG, "Passkey action: NONE (no passkey required)");
        break;

    case BLE_SM_IOACT_INPUT:
        ESP_LOGI(TAG, "Passkey action: INPUT (client should input passkey)");
        break;

    case BLE_SM_IOACT_NUMCMP:
        ESP_LOGI(TAG, "Passkey action: NUMCMP (numeric comparison)");
        break;

    default:
        ESP_LOGW(TAG, "Unknown passkey action: %d", event->passkey.params.action);
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

            // Check if device is bonded or if we have stored bond information
            bool is_bonded = desc.sec_state.bonded;
            bool pairing_mode = (event_manager_get_bits() & EVENT_BIT_PAIRING_MODE) != 0;

            // If not marked as bonded yet, check if we have stored bond info
            if (!is_bonded && desc.peer_id_addr.type <= BLE_ADDR_RANDOM_ID)
            {
                struct ble_store_key_sec key_sec;
                struct ble_store_value_sec sec;

                memset(&key_sec, 0, sizeof(key_sec));
                memcpy(key_sec.peer_addr.val, desc.peer_id_addr.val, 6);
                key_sec.peer_addr.type = desc.peer_id_addr.type;

                // Try to read stored security info (bond)
                rc = ble_store_read_peer_sec(&key_sec, &sec);
                if (rc == 0)
                {
                    is_bonded = true;
                    ESP_LOGI(TAG, "Found stored bond information for peer");
                }
            }

            // Security disabled - allow all connections without pairing
            ESP_LOGI(TAG, "Connection accepted - security disabled, no pairing required");

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
        ESP_LOGI(TAG, "PASSKEY_ACTION event received: conn_handle=%d, action=%d",
                 event->passkey.conn_handle, event->passkey.params.action);
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
                ESP_LOGI(TAG, "Security state: encrypted=%d, authenticated=%d, bonded=%d",
                         desc.sec_state.encrypted, desc.sec_state.authenticated, desc.sec_state.bonded);
                if (passkey_conn_handle == event->enc_change.conn_handle)
                {
                    passkey_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                    current_passkey = 0;
                }

                bool pairing_mode = (event_manager_get_bits() & EVENT_BIT_PAIRING_MODE) != 0;

                // If in pairing mode and encryption successful, device is now paired
                if (pairing_mode)
                {
                    // Device successfully paired - set connected bit so pairing task can continue
                    ESP_LOGI(TAG, "Device successfully paired in pairing mode");
                    if (desc.sec_state.bonded)
                    {
                        ESP_LOGI(TAG, "Device is now bonded and will be saved to NVS");
                    }
                    else
                    {
                        ESP_LOGW(TAG, "Warning: Device paired but bond state is not set!");
                    }
                    event_manager_set_bits(EVENT_BIT_PAIRING_SUCCESS);
                }
                else
                {
                    // Encryption successful - device is connected (bonded or using stored keys)
                    ESP_LOGI(TAG, "Device connected with encryption");
                    if (desc.sec_state.bonded)
                    {
                        ESP_LOGI(TAG, "Bonded device reconnected");
                    }
                    event_manager_set_bits(EVENT_BIT_BLE_CONNECTED);
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

    case BLE_GAP_EVENT_SUBSCRIBE:
    {
        uint16_t attr_handle = event->subscribe.attr_handle;
        if (event->subscribe.cur_notify || event->subscribe.cur_indicate)
        {
            ESP_LOGI(TAG, "✓ Client SUBSCRIBED: conn_handle=%d, attr_handle=%d, reason=%d (notify=%s, indicate=%s)",
                     event->subscribe.conn_handle, attr_handle, event->subscribe.reason,
                     event->subscribe.cur_notify ? "YES" : "NO",
                     event->subscribe.cur_indicate ? "YES" : "NO");
        }
        else
        {
            ESP_LOGI(TAG, "✗ Client UNSUBSCRIBED: conn_handle=%d, attr_handle=%d, reason=%d",
                     event->subscribe.conn_handle, attr_handle, event->subscribe.reason);
        }
        return rc;
    }
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

int gap_init(void)
{
    int rc = 0;

    ble_svc_gap_init();

    rc = ble_svc_gap_device_name_set(DEVICE_NAME);

    // Disable security - allow anyone to connect without pairing
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 0; // Disable bonding
    ble_hs_cfg.sm_mitm = 0;    // Disable MITM protection (no passkey required)
    ble_hs_cfg.sm_sc = 0;      // Legacy pairing (not secure connections)
    ble_hs_cfg.sm_our_key_dist = 0;
    ble_hs_cfg.sm_their_key_dist = 0;

    ESP_LOGI(TAG, "Security manager configured: io_cap=%d, bonding=%d, mitm=%d, sc=%d",
             ble_hs_cfg.sm_io_cap, ble_hs_cfg.sm_bonding, ble_hs_cfg.sm_mitm, ble_hs_cfg.sm_sc);

    return rc;
}
