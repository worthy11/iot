/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Includes */
#include "gap.h"
#include "common.h"
#include "gatt_svc.h"
#include "ssd1306_demo.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "store/config/ble_store_config.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "esp_random.h"

static const char *TAG = "gap";

/* Private function declarations */
inline static void format_addr(char *addr_str, uint8_t addr[]);
static void start_advertising(void);
static int gap_event_handler(struct ble_gap_event *event, void *arg);

/* Private variables */
static uint8_t own_addr_type;
static uint8_t addr_val[6] = {0};
static uint32_t current_passkey = 0;
static uint16_t passkey_conn_handle = BLE_HS_CONN_HANDLE_NONE;

/* Private functions */
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

    switch (event->passkey.params.action)
    {
    case BLE_SM_IOACT_DISP:
        current_passkey = generate_passkey();
        passkey_conn_handle = event->passkey.conn_handle;

        ESP_LOGI(TAG, "Passkey: %06lu", (unsigned long)current_passkey);

        ssd1306_demo_display_passkey(current_passkey);
        struct ble_sm_io io_data = {
            .action = BLE_SM_IOACT_DISP,
            .passkey = current_passkey};
        rc = ble_sm_inject_io(event->passkey.conn_handle, &io_data);
        if (rc != 0)
        {
            ESP_LOGE(TAG, "Failed to inject passkey: %d (0x%04x)", rc, rc);
        }
        break;
    }

    return rc;
}

static void start_advertising(void)
{
    /* Local variables */
    int rc = 0;
    const char *name;
    struct ble_hs_adv_fields adv_fields = {0};
    struct ble_hs_adv_fields rsp_fields = {0};
    struct ble_gap_adv_params adv_params = {0};
    /* Service UUIDs to advertise: Battery Service (0x180F) and custom WiFi Config Service */
    static const ble_uuid16_t adv_uuids16[] = {
        BLE_UUID16_INIT(0x180F)};

    /* Set advertising flags */
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* Set device appearance - Generic (0x0000) or remove appearance field */
    adv_fields.appearance = 0x0000;
    adv_fields.appearance_is_present = 1;

    /* Set device name in main advertising data (so clients can see it without scan response) */
    name = ble_svc_gap_device_name();
    if (name == NULL)
    {
        ESP_LOGW(TAG, "Device name is NULL, using DEVICE_NAME directly");
        name = DEVICE_NAME;
    }
    ESP_LOGI(TAG, "Advertising device name: %s", name);
    adv_fields.name = (uint8_t *)name;
    adv_fields.name_len = strlen(name);
    adv_fields.name_is_complete = 1;

    /* Set advertiement fields */
    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to set advertising data, error code: %d", rc);
        return;
    }

    /* Advertise service UUIDs in scan response: HID Service and vendor service */
    rsp_fields.uuids16 = adv_uuids16;
    rsp_fields.num_uuids16 = sizeof(adv_uuids16) / sizeof(adv_uuids16[0]);
    rsp_fields.uuids16_is_complete = 1;

    /* Set scan response fields */
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to set scan response data, error code: %d", rc);
        return;
    }

    /* Set connectable and general discoverable mode */
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    /* Set advertising interval */
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(500);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(510);

    /* Start advertising */
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           gap_event_handler, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to start advertising, error code: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "advertising started!");
}

/*
 * NimBLE applies an event-driven model to keep GAP service going
 * gap_event_handler is a callback function registered when calling
 * ble_gap_adv_start API and called when a GAP event arrives
 */
static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    /* Local variables */
    int rc = 0;
    struct ble_gap_conn_desc desc;

    /* Handle different GAP event */
    switch (event->type)
    {

    /* Connect event */
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        if (event->connect.status == 0)
        {
            /* Check connection handle */
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc != 0)
            {
                ESP_LOGE(TAG,
                         "failed to find connection by handle, error code: %d",
                         rc);
                return rc;
            }

            ESP_LOGI(TAG, "Connection security: encrypted=%d, authenticated=%d, bonded=%d",
                     desc.sec_state.encrypted, desc.sec_state.authenticated, desc.sec_state.bonded);

            if (desc.sec_state.bonded)
            {
                ESP_LOGI(TAG, "Device is already bonded, clearing bond to force passkey entry...");
                ble_store_clear();
                ESP_LOGI(TAG, "Bond cleared, disconnecting to force re-pairing with passkey...");
                ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                return rc;
            }

            ESP_LOGI(TAG, "Initiating pairing/encryption...");
            ble_store_clear();
            rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc != 0)
            {
                ESP_LOGE(TAG, "Failed to initiate security: %d", rc);
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
                ssd1306_demo_clear_passkey();
                passkey_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                current_passkey = 0;
            }
        }
        else
        {
            start_advertising();
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
                    ssd1306_demo_clear_passkey();
                    passkey_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                    current_passkey = 0;
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
            ssd1306_demo_clear_passkey();
            passkey_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            current_passkey = 0;
        }

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

    case BLE_GAP_EVENT_SUBSCRIBE:
        gatt_svr_subscribe_cb(event);
        return rc;

    case BLE_GAP_EVENT_MTU:
        return rc;
    }

    return rc;
}

/* Public functions */
void adv_init(void)
{
    /* Local variables */
    int rc = 0;
    char addr_str[18] = {0};

    /* Make sure we have proper BT identity address set (random preferred) */
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "device does not have any available bt address!");
        return;
    }

    /* Figure out BT address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to infer address type, error code: %d", rc);
        return;
    }

    /* Printing ADDR */
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to copy device address, error code: %d", rc);
        return;
    }
    format_addr(addr_str, addr_val);
    ESP_LOGI(TAG, "device address: %s", addr_str);

    /* Start advertising. */
    start_advertising();
}

int gap_init(void)
{
    /* Local variables */
    int rc = 0;

    /* Call NimBLE GAP initialization API */
    ble_svc_gap_init();

    /* Set GAP device name */
    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to set device name to %s, error code: %d",
                 DEVICE_NAME, rc);
        return rc;
    }

    /* Configure security manager for passkey pairing */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY; // Display only - show passkey
    ble_hs_cfg.sm_bonding = 0;                      // Enable bonding
    ble_hs_cfg.sm_mitm = 1;                         // Require MITM protection (passkey)
    ble_hs_cfg.sm_sc = 0;                           // Legacy pairing (not secure connections)
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    // No need to seed - using ESP32 hardware RNG (esp_random) for passkey generation

    return rc;
}
