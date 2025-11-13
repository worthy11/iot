#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"

#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "host/ble_sm.h"
#include "host/util/util.h"

#include "ble_better.h"
#include "ble_utils.h"

#define MAX_CANDIDATES 5
static const char *TAG = "ble_manager";

typedef struct
{
    ble_addr_t addr;
    uint16_t conn_handle;
    bool connected;
    uint16_t battery_handle;
    uint16_t alert_handle;
} candidate_t;

static candidate_t candidates[MAX_CANDIDATES];
static candidate_t *s_active_device = NULL;
static uint8_t own_addr_type;
static bool connecting = false;
static char s_target_device_name[32] = "";

#define NVS_NAMESPACE "ble_config"
#define NVS_KEY_DEVICE_NAME "device_name"

#define HID_REPORT_LEN 8
#define MAX_KEYS 6

static uint8_t prev_keys[MAX_KEYS] = {0};

// static void parse_hid_report(const uint8_t *data, int len);
// static bool in_prev(uint8_t code);
// static bool in_current(uint8_t code, const uint8_t *keys);

// static void parse_hid_report(const uint8_t *data, int len)
// {
//     if (len < HID_REPORT_LEN)
//         return;

//     uint8_t mods = data[0];
//     const uint8_t *keys = &data[2];

//     ESP_LOGI("HID", "Modifiers: 0x%02X", mods);

//     for (int i = 0; i < MAX_KEYS; i++)
//     {
//         uint8_t code = keys[i];
//         if (code != 0 && !in_prev(code))
//         {
//             ESP_LOGI("HID", "Key down: 0x%02X", code);
//         }
//     }

//     for (int i = 0; i < MAX_KEYS; i++)
//     {
//         if (prev_keys[i] != 0 && !in_current(prev_keys[i], keys))
//         {
//             ESP_LOGI("HID", "Key up: 0x%02X", prev_keys[i]);
//         }
//     }

//     memcpy(prev_keys, keys, MAX_KEYS);
// }

// static bool in_prev(uint8_t code)
// {
//     for (int i = 0; i < MAX_KEYS; i++)
//         if (prev_keys[i] == code)
//             return true;
//     return false;
// }

// static bool in_current(uint8_t code, const uint8_t *keys)
// {
//     for (int i = 0; i < MAX_KEYS; i++)
//         if (keys[i] == code)
//             return true;
//     return false;
// }

void parse_adv_fields(const uint8_t *adv_data, int adv_len)
{
    struct ble_hs_adv_fields fields;
    int rc;

    rc = ble_hs_adv_parse_fields(&fields, adv_data, adv_len);
    if (rc != 0)
    {
        printf("Failed to parse adv fields: %d\n", rc);
        return;
    }

    printf("Flags: 0x%02X\n", fields.flags);
}

static int gap_event_handler(struct ble_gap_event *event, void *arg);
static void start_scan(void);
static int read_battery(void);
static int send_alert(uint8_t level);
static void load_device_name_from_nvs(void);
static void save_device_name_to_nvs(const char *device_name);
static void clear_device_name_from_nvs(void);

static int battery_read_cb(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    if (error->status == 0 && attr != NULL && attr->om != NULL)
    {
        uint8_t battery_level = attr->om->om_data[0];
        ESP_LOGI(TAG, "Battery level: %u%%", battery_level);
        printf("Battery level: %u%%\n", battery_level);
    }
    else
    {
        ESP_LOGE(TAG, "Battery read failed: status=%d", error->status);
        printf("Battery read failed: status=%d\n", error->status);
    }
    return 0;
}

static int read_battery(void)
{
    if (!s_active_device)
    {
        ESP_LOGE(TAG, "No active connection");
        printf("Error: No active connection\n");
        return -1;
    }

    if (s_active_device->battery_handle == 0)
    {
        ESP_LOGE(TAG, "Battery characteristic not discovered");
        printf("Error: Battery characteristic not discovered\n");
        return -1;
    }

    ESP_LOGI(TAG, "Reading battery from handle %d", s_active_device->battery_handle);
    int rc = ble_gattc_read(s_active_device->conn_handle, s_active_device->battery_handle, battery_read_cb, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to initiate battery read: %d", rc);
        printf("Error: Failed to initiate battery read: %d\n", rc);
        return -1;
    }

    return 0;
}

int cmd_read_battery(int argc, char **argv)
{
    return read_battery();
}

static int alert_write_cb(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    if (error->status == 0)
    {
        ESP_LOGI(TAG, "Alert write successful");
        printf("Alert sent successfully\n");
    }
    else
    {
        ESP_LOGE(TAG, "Alert write failed: status=%d", error->status);
        printf("Alert write failed: status=%d\n", error->status);
    }
    return 0;
}

static int send_alert(uint8_t level)
{
    if (!s_active_device)
    {
        ESP_LOGE(TAG, "No active connection");
        printf("Error: No active connection\n");
        return -1;
    }

    if (s_active_device->alert_handle == 0)
    {
        ESP_LOGE(TAG, "Alert characteristic not discovered");
        printf("Error: Alert characteristic not discovered\n");
        return -1;
    }

    ESP_LOGI(TAG, "Sending alert level %u to handle %d", level, s_active_device->alert_handle);
    int rc = ble_gattc_write_flat(s_active_device->conn_handle, s_active_device->alert_handle, &level, 1, alert_write_cb, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to initiate alert write: %d", rc);
        printf("Error: Failed to initiate alert write: %d\n", rc);
        return -1;
    }

    return 0;
}

static int cmd_send_alert(int argc, char **argv)
{
    uint8_t level = 2;

    if (argc > 1)
    {
        int parsed = atoi(argv[1]);
        if (parsed < 0 || parsed > 2)
        {
            printf("Error: Alert level must be 0 (no alert), 1 (mild), or 2 (high)\n");
            return -1;
        }
        level = (uint8_t)parsed;
    }

    return send_alert(level);
}

static int cmd_connect(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: connect <device_name>\n");
        printf("Supported devices: itag, aquatest, popicon\n");
        return -1;
    }

    char *normalized = normalize_name((const uint8_t *)argv[1], strlen(argv[1]));
    if (s_active_device && s_active_device->connected)
    {
        printf("Already connected. Disconnect first or use 'disconnect'\n");
        return -1;
    }

    strncpy(s_target_device_name, normalized, sizeof(s_target_device_name) - 1);
    s_target_device_name[sizeof(s_target_device_name) - 1] = '\0';

    save_device_name_to_nvs(s_target_device_name);
    ble_gap_disc_cancel();

    connecting = false;
    for (int i = 0; i < MAX_CANDIDATES; i++)
    {
        if (!candidates[i].connected)
        {
            memset(&candidates[i].addr, 0, sizeof(ble_addr_t));
        }
    }

    ESP_LOGI(TAG, "Connecting to device: %s\n", argv[1]);
    start_scan();
    return 0;
}

static int cmd_disconnect(int argc, char **argv)
{
    if (!s_active_device || !s_active_device->connected)
    {
        printf("Not connected\n");
        return -1;
    }

    printf("Disconnecting...\n");
    clear_device_name_from_nvs();
    s_target_device_name[0] = '\0';

    ble_gap_terminate(s_active_device->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static void register_console_commands(void)
{
    const esp_console_cmd_t cmd_battery = {
        .command = "battery",
        .help = "Read battery level from connected device",
        .hint = NULL,
        .func = &cmd_read_battery,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_battery));

    const esp_console_cmd_t cmd_alert = {
        .command = "alert",
        .help = "Send alert to iTag (usage: alert [0|1|2], default=2)",
        .hint = NULL,
        .func = &cmd_send_alert,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_alert));

    const esp_console_cmd_t cmd_connect_cfg = {
        .command = "connect",
        .help = "Connect to a BLE device (usage: connect <device_name>)",
        .hint = NULL,
        .func = &cmd_connect,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_connect_cfg));

    const esp_console_cmd_t cmd_disconnect_cfg = {
        .command = "disconnect",
        .help = "Disconnect from current device",
        .hint = NULL,
        .func = &cmd_disconnect,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_disconnect_cfg));

    ESP_LOGI(TAG, "Console commands registered: 'battery', 'alert', 'connect', 'disconnect'");
}

static int dsc_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error, uint16_t end_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    if (error->status != 0)
    {
        ESP_LOGI(TAG, "Descriptor discovery complete");
        return 0;
    }

    if (dsc->uuid.u.type == BLE_UUID_TYPE_16)
    {
        uint16_t u = dsc->uuid.u16.value;
        ESP_LOGI(TAG, "Discovered descriptor 0x%04x, handle=%d", u, dsc->handle);
    }
    else
    {
        ESP_LOGI(TAG, "Discovered descriptor (128-bit UUID), handle=%d", dsc->handle);
    }

    return 0;
}

static int char_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr, void *arg)
{
    candidate_t *candidate = (candidate_t *)arg;

    if (error->status != 0)
    {
        ESP_LOGI(TAG, "Characteristic discovery complete");
        return 0;
    }

    if (!chr)
    {
        return 0;
    }

    if (chr->uuid.u.type == BLE_UUID_TYPE_16)
    {
        uint16_t u = chr->uuid.u16.value;
        ESP_LOGI(TAG, "Discovered characteristic 0x%04x, handle=%d", u, chr->val_handle);

        if (u == 0x2A19)
        {
            candidate->battery_handle = chr->val_handle;
            ESP_LOGI(TAG, "Battery Level characteristic found, handle=%d", chr->val_handle);
        }
        else if (u == 0x2A06)
        {
            candidate->alert_handle = chr->val_handle;
            ESP_LOGI(TAG, "Alert Level characteristic found, handle=%d", chr->val_handle);
        }
    }
    else
    {
        ESP_LOGI(TAG, "Discovered characteristic (128-bit UUID), handle=%d", chr->val_handle);
    }

    ble_gattc_disc_all_dscs(conn_handle, chr->val_handle, chr->val_handle + 10, dsc_disc_cb, candidate);
    return 0;
}

static int svc_disc_cb(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_svc *svc, void *arg)
{
    candidate_t *candidate = (candidate_t *)arg;

    if (error->status != 0)
    {
        if (error->status == 0x05 || error->status == 0x0F || error->status == 0x10)
        {
            ESP_LOGI(TAG, "Service discovery error (status=0x%02x) - requires pairing", error->status);
        }
        else if (error->status == 0x0E)
        {
            ESP_LOGD(TAG, "Service discovery: Attribute Not Found (0x0E) - this is normal");
        }
        else
        {
            ESP_LOGE(TAG, "Service discovery error: %d (0x%02x)", error->status, error->status);
        }
        return 0;
    }

    if (!svc)
    {
        ESP_LOGI(TAG, "Service discovery complete");
        return 0;
    }

    if (svc->uuid.u.type == BLE_UUID_TYPE_16)
    {
        uint16_t u = svc->uuid.u16.value;
        if (u == 0x1812)
        {
            ESP_LOGI(TAG, "Found HID service (0x1812)");
        }
        else if (u == 0x180F)
        {
            ESP_LOGI(TAG, "Found battery service (0x180F)");
        }
        else if (u == 0x1802)
        {
            ESP_LOGI(TAG, "Found Immediate Alert service (0x1802)");
        }
        else if (u == 0x180A)
        {
            ESP_LOGI(TAG, "Found Device Info service (0x180A)");
        }
        ble_gattc_disc_all_chrs(conn_handle, svc->start_handle, svc->end_handle, char_disc_cb, candidate);
    }
    return 0;
}

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
    {
        if (s_target_device_name[0] == '\0')
        {
            return 0;
        }

        struct ble_hs_adv_fields fields;
        int rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
        bool device_matched = false;
        char *normalized_name = "";

        if (rc == 0 && fields.name && fields.name_len > 0)
        {
            normalized_name = normalize_name(fields.name, fields.name_len);
        }

        if (normalized_name && normalized_name[0] != '\0' && strcmp(normalized_name, s_target_device_name) == 0)
        {
            device_matched = true;
            ESP_LOGI(TAG, "Found target device by name: %.*s", fields.name_len, fields.name);
        }

        if (!device_matched && (strcmp(s_target_device_name, "itag") == 0 || strcmp(s_target_device_name, "aquatest") == 0))
        {
            if (rc == 0 && fields.uuids16 != NULL && fields.num_uuids16 > 0)
            {
                for (int i = 0; i < fields.num_uuids16; i++)
                {
                    if (fields.uuids16[i].value == 0x1802 || fields.uuids16[i].value == 0x180F)
                    {
                        device_matched = true;
                        ESP_LOGI(TAG, "Found iTag/aquatest by service UUID: 0x%04x", fields.uuids16[i].value);
                        break;
                    }
                }
            }

            if (!device_matched && event->disc.data != NULL && event->disc.length_data > 0)
            {
                for (int i = 0; i <= (int)event->disc.length_data - 4; i++)
                {
                    if ((event->disc.data[i] == 0x69 || event->disc.data[i] == 0x49) &&
                        (event->disc.data[i + 1] == 0x54 || event->disc.data[i + 1] == 0x74) &&
                        (event->disc.data[i + 2] == 0x41 || event->disc.data[i + 2] == 0x61) &&
                        (event->disc.data[i + 3] == 0x47 || event->disc.data[i + 3] == 0x67))
                    {
                        device_matched = true;
                        ESP_LOGI(TAG, "Found iTag by name pattern in raw data");
                        break;
                    }
                }
            }
        }

        if (device_matched)
        {
            ESP_LOGI(TAG, "Found target device (%s): addr=%s", s_target_device_name, ble_addr_to_str(&event->disc.addr));
            {
                for (int i = 0; i < MAX_CANDIDATES; i++)
                {
                    if (candidates[i].connected &&
                        ble_addr_cmp(&candidates[i].addr, &event->disc.addr) == 0)
                    {
                        break;
                    }
                    if (!is_addr_empty(&candidates[i].addr) &&
                        ble_addr_cmp(&candidates[i].addr, &event->disc.addr) == 0 &&
                        !candidates[i].connected)
                    {
                        break;
                    }
                }

                for (int i = 0; i < MAX_CANDIDATES; i++)
                {
                    if (candidates[i].connected && candidates[i].conn_handle != BLE_HS_CONN_HANDLE_NONE)
                    {
                        break;
                    }
                }

                for (int i = 0; i < MAX_CANDIDATES; i++)
                {
                    if (!candidates[i].connected && is_addr_empty(&candidates[i].addr))
                    {
                        candidates[i].addr = event->disc.addr;

                        if (ble_gap_disc_cancel() != 0)
                        {
                            ESP_LOGW(TAG, "Failed to cancel scan before connect");
                        }

                        connecting = true;
                        int rc = ble_gap_connect(own_addr_type, &event->disc.addr, 30000, NULL, gap_event_handler, NULL);
                        if (rc != 0)
                        {
                            ESP_LOGE(TAG, "Failed to initiate connection: %d", rc);
                            connecting = false;
                            memset(&candidates[i].addr, 0, sizeof(ble_addr_t));
                            start_scan();
                        }
                        else
                        {
                            ESP_LOGI(TAG, "Connection attempt initiated");
                        }
                        break;
                    }
                }
            }
        }
        else if (rc == 0 && fields.name && fields.name_len > 0)
        {
            ESP_LOGI(TAG, "Found device: %.*s, addr=%s", fields.name_len, fields.name,
                     ble_addr_to_str(&event->disc.addr));
        }
        break;
    }

    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE_GAP_EVENT_CONNECT: conn=%d, status=%d",
                 event->connect.conn_handle, event->connect.status);
        connecting = false;
        if (event->connect.status == 0)
        {
            ESP_LOGI(TAG, "Searching for empty slot");
            for (int i = 0; i < MAX_CANDIDATES; i++)
            {
                ESP_LOGI(TAG, "Found candidate %s type=%d (empty=%d)",
                         ble_addr_to_str(&candidates[i].addr),
                         candidates[i].addr.type,
                         is_addr_empty(&candidates[i].addr));
                if (!candidates[i].connected && !is_addr_empty(&candidates[i].addr))
                {
                    ESP_LOGI(TAG, "Found empty slot");
                    candidates[i].conn_handle = event->connect.conn_handle;
                    candidates[i].connected = true;
                    candidates[i].battery_handle = 0;
                    candidates[i].alert_handle = 0;
                    s_active_device = &candidates[i];
                    ESP_LOGI(TAG, "Connected to %s", ble_addr_to_str(&candidates[i].addr));
                    ESP_LOGI(TAG, "Starting service discovery...");
                    ble_gattc_disc_all_svcs(event->connect.conn_handle, svc_disc_cb, &candidates[i]);
                    break;
                }
            }
        }
        else
        {
            ESP_LOGE(TAG, "Connection failed with status: %d", event->connect.status);
            for (int i = 0; i < MAX_CANDIDATES; i++)
            {
                if (!is_addr_empty(&candidates[i].addr) && !candidates[i].connected)
                {
                    memset(&candidates[i].addr, 0, sizeof(ble_addr_t));
                }
            }
            start_scan();
        }
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "BLE_GAP_EVENT_SUBSCRIBE: conn=%d, handle=0x%04x, notify=%d, indicate=%d",
                 event->subscribe.conn_handle,
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify,
                 event->subscribe.cur_indicate);
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGI(TAG, "BLE_GAP_EVENT_PASSKEY_ACTION: conn=%d, action=%d",
                 event->passkey.conn_handle, event->passkey.params.action);
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "BLE_GAP_EVENT_ENC_CHANGE: conn=%d, status=%d",
                 event->enc_change.conn_handle,
                 event->enc_change.status);
        if (event->enc_change.status == 0)
        {
            ESP_LOGI(TAG, "Encryption enabled; device bonded (conn=%d)", event->enc_change.conn_handle);
        }
        else
        {
            ESP_LOGW(TAG, "Encryption/bonding failed, status=%d", event->enc_change.status);
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE_GAP_EVENT_DISCONNECT: conn=%d, reason=0x%02x", event->disconnect.conn.conn_handle, event->disconnect.reason);
        connecting = false;
        for (int i = 0; i < MAX_CANDIDATES; i++)
        {
            if (candidates[i].conn_handle == event->disconnect.conn.conn_handle)
            {
                candidates[i].connected = false;
                candidates[i].conn_handle = 0;
                if (s_active_device == &candidates[i])
                {
                    s_active_device = NULL;
                }
            }
        }
        break;

    default:
        ESP_LOGW(TAG, "Unhandled GAP event: type=%d (0x%02x)", event->type, event->type);
        break;
    }
    return 0;
}

static void load_device_name_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGD(TAG, "NVS open failed: %s (namespace may not exist yet)", esp_err_to_name(err));
        return;
    }

    size_t required_size = sizeof(s_target_device_name);
    err = nvs_get_str(nvs_handle, NVS_KEY_DEVICE_NAME, s_target_device_name, &required_size);
    nvs_close(nvs_handle);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Loaded device name from NVS: %s", s_target_device_name);
    }
    else if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGD(TAG, "No stored device name found in NVS");
    }
    else
    {
        ESP_LOGW(TAG, "Failed to read device name from NVS: %s", esp_err_to_name(err));
    }
}

static void save_device_name_to_nvs(const char *device_name)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_str(nvs_handle, NVS_KEY_DEVICE_NAME, device_name);
    if (err == ESP_OK)
    {
        err = nvs_commit(nvs_handle);
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "Saved device name to NVS: %s", device_name);
        }
        else
        {
            ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
        }
    }
    else
    {
        ESP_LOGE(TAG, "NVS set_str failed: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
}

static void clear_device_name_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_erase_key(nvs_handle, NVS_KEY_DEVICE_NAME);
    if (err == ESP_OK)
    {
        err = nvs_commit(nvs_handle);
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "Cleared device name from NVS");
        }
        else
        {
            ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
        }
    }
    else if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGD(TAG, "Device name key not found in NVS (already cleared)");
    }
    else
    {
        ESP_LOGE(TAG, "NVS erase_key failed: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting state; reason=%d", reason);
}

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0)
    {
        ESP_LOGE(TAG, "Failed to ensure address");
        return;
    }

    load_device_name_from_nvs();
    if (s_target_device_name[0] != '\0')
    {
        ESP_LOGI(TAG, "Auto-connecting to stored device: %s", s_target_device_name);
        start_scan();
    }
    else
    {
        ESP_LOGI(TAG, "BLE ready. Use 'connect <device_name>' to start scanning");
    }
}

static void host_task(void *param)
{
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void start_scan(void)
{
    struct ble_gap_disc_params params = {
        .passive = 1,
        .filter_duplicates = 1,
    };

    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0)
    {
        ESP_LOGE(TAG, "addr type inference failed");
        return;
    }

    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params, gap_event_handler, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
    }
}

void init_ble_manager(void)
{
    ESP_ERROR_CHECK(nimble_port_init());

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_KEYBOARD_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_device_name_set("esp32-ble-client");
    nimble_port_freertos_init(host_task);
    register_console_commands();
}