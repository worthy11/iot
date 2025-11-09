#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "ble_manager.h"

static const char *TAG = "ble_manager";

#define UUID_IMMEDIATE_ALERT 0x1802
#define UUID_ALERT_LEVEL 0x2A06
#define UUID_BATTERY_SERVICE 0x180F
#define UUID_BATTERY_LEVEL 0x2A19
#define UUID_VENDOR_SERVICE 0xFFE0
#define UUID_VENDOR_STATE 0xFFE1

typedef enum
{
    ITAG_CHAR_ALERT_LEVEL = 0,
    ITAG_CHAR_BATTERY_LEVEL,
    ITAG_CHAR_VENDOR_STATE,
    ITAG_CHAR_COUNT
} itag_char_id_t;

typedef struct
{
    bool present;
    uint16_t value_handle;
    uint16_t cccd_handle;
} itag_characteristic_t;

typedef struct
{
    uint16_t conn_handle;
    bool initial_reads_requested;
    itag_characteristic_t chars[ITAG_CHAR_COUNT];
} itag_ctx_t;

static itag_ctx_t s_ctx = {
    .conn_handle = BLE_HS_CONN_HANDLE_NONE,
};
static bool s_connecting = false;
static TaskHandle_t s_alert_task;

#define ITAG_ALERT_LEVEL 1
#define ITAG_ALERT_INTERVAL_MS 10000
#define ITAG_ALERT_IDLE_DELAY_MS 1000

static void start_scan(void);
static int gap_event(struct ble_gap_event *event, void *arg);
static int itag_on_service(uint16_t conn_handle,
                           const struct ble_gatt_error *error,
                           const struct ble_gatt_svc *svc,
                           void *arg);
static int itag_on_characteristic(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  const struct ble_gatt_chr *chr,
                                  void *arg);
static bool extract_name_field(const uint8_t *data,
                               uint8_t len,
                               char *out,
                               size_t out_len);
static void log_adv_payload(const char *prefix, const char *addr_str, const uint8_t *data, uint8_t len);
static void itag_read_task(void *param);
void ble_manager_read_battery_level();

static bool extract_name_field(const uint8_t *data,
                               uint8_t len,
                               char *out,
                               size_t out_len)
{
    if (data == NULL || len == 0 || out == NULL || out_len == 0)
    {
        return false;
    }

    bool found = false;
    size_t offset = 0;

    while (offset < len)
    {
        uint8_t field_len = data[offset];
        offset++;

        if (field_len == 0)
        {
            break;
        }

        if (offset + field_len > len)
        {
            ESP_LOGD(TAG, "AD field overruns payload (offset=%u len=%u total=%u)",
                     (unsigned)offset - 1, field_len, len);
            break;
        }

        if (field_len < 1)
        {
            continue;
        }

        uint8_t type = data[offset];
        offset++;
        uint8_t value_len = field_len - 1;

        if ((type == BLE_HS_ADV_TYPE_COMP_NAME) ||
            (type == BLE_HS_ADV_TYPE_INCOMP_NAME))
        {
            size_t copy_len = value_len;
            if (copy_len >= out_len)
            {
                copy_len = out_len - 1;
            }
            memcpy(out, &data[offset], copy_len);
            out[copy_len] = '\0';
            found = true;
            break;
        }

        offset += value_len;
    }

    return found;
}

static void log_adv_payload(const char *prefix, const char *addr_str, const uint8_t *data, uint8_t len)
{
    if (data == NULL || len == 0)
    {
        return;
    }

    ESP_LOGI(TAG, "%s payload addr=%s len=%u", prefix, addr_str, len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);
}

static void itag_read_task(void *param)
{
    (void)param;

    const TickType_t alert_delay = pdMS_TO_TICKS(ITAG_ALERT_INTERVAL_MS);
    const TickType_t idle_delay = pdMS_TO_TICKS(ITAG_ALERT_IDLE_DELAY_MS);

    while (1)
    {
        if (s_ctx.conn_handle != BLE_HS_CONN_HANDLE_NONE &&
            s_ctx.chars[ITAG_CHAR_BATTERY_LEVEL].present)
        {
            // ble_manager_trigger_beep(ITAG_ALERT_LEVEL);
            ble_manager_read_battery_level();
            vTaskDelay(alert_delay);
        }
        else
        {
            vTaskDelay(idle_delay);
        }
    }
}

static int itag_read_cb(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr,
                        void *arg)
{
    itag_char_id_t id = (itag_char_id_t)(uintptr_t)arg;

    if (error->status != 0)
    {
        ESP_LOGW(TAG, "Read failed (char %d, status=%d)", id, error->status);
        return 0;
    }

    uint8_t buf[32];
    uint16_t len = OS_MBUF_PKTLEN(attr->om);
    if (len > sizeof(buf))
    {
        len = sizeof(buf);
    }
    os_mbuf_copydata(attr->om, 0, len, buf);

    switch (id)
    {
    case ITAG_CHAR_BATTERY_LEVEL:
        if (len > 0)
        {
            ESP_LOGI(TAG, "Battery level: %u%%", buf[0]);
        }
        else
        {
            ESP_LOGI(TAG, "Battery level: <empty>");
        }
        break;

    case ITAG_CHAR_VENDOR_STATE:
        ESP_LOGI(TAG, "Vendor state (%u bytes)", len);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, len, ESP_LOG_INFO);
        break;

    default:
        ESP_LOGI(TAG, "Characteristic %d read (%u bytes)", id, len);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, len, ESP_LOG_INFO);
        break;
    }

    return 0;
}

static int itag_write_cb(uint16_t conn_handle,
                         const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr,
                         void *arg)
{
    itag_char_id_t id = (itag_char_id_t)(uintptr_t)arg;

    if (error->status != 0)
    {
        ESP_LOGW(TAG, "Write failed (char %d, status=%d)", id, error->status);
        return 0;
    }

    ESP_LOGI(TAG, "Write OK (char %d, handle=0x%04x)", id, attr->handle);
    return 0;
}

static int itag_on_characteristic(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  const struct ble_gatt_chr *chr,
                                  void *arg)
{
    if (error->status == BLE_HS_EDONE)
    {
        if (!s_ctx.initial_reads_requested &&
            s_ctx.chars[ITAG_CHAR_BATTERY_LEVEL].present)
        {
            int rc = ble_gattc_read(conn_handle,
                                    s_ctx.chars[ITAG_CHAR_BATTERY_LEVEL].value_handle,
                                    itag_read_cb,
                                    (void *)(uintptr_t)ITAG_CHAR_BATTERY_LEVEL);
            if (rc != 0)
            {
                ESP_LOGW(TAG, "Failed to read battery level: %d", rc);
            }
            else
            {
                s_ctx.initial_reads_requested = true;
            }
        }
        return 0;
    }

    if (error->status != 0)
    {
        ESP_LOGE(TAG, "Characteristic discovery error: %d", error->status);
        return error->status;
    }

    const ble_uuid_t *uuid = &chr->uuid.u;

    if (ble_uuid_cmp(uuid, BLE_UUID16_DECLARE(UUID_ALERT_LEVEL)) == 0)
    {
        s_ctx.chars[ITAG_CHAR_ALERT_LEVEL].present = true;
        s_ctx.chars[ITAG_CHAR_ALERT_LEVEL].value_handle = chr->val_handle;
        ESP_LOGI(TAG, "Alert Level characteristic handle=0x%04x", chr->val_handle);
    }
    else if (ble_uuid_cmp(uuid, BLE_UUID16_DECLARE(UUID_BATTERY_LEVEL)) == 0)
    {
        s_ctx.chars[ITAG_CHAR_BATTERY_LEVEL].present = true;
        s_ctx.chars[ITAG_CHAR_BATTERY_LEVEL].value_handle = chr->val_handle;
        ESP_LOGI(TAG, "Battery Level characteristic handle=0x%04x", chr->val_handle);
    }
    else if (ble_uuid_cmp(uuid, BLE_UUID16_DECLARE(UUID_VENDOR_STATE)) == 0)
    {
        s_ctx.chars[ITAG_CHAR_VENDOR_STATE].present = true;
        s_ctx.chars[ITAG_CHAR_VENDOR_STATE].value_handle = chr->val_handle;
        ESP_LOGI(TAG, "Vendor characteristic handle=0x%04x", chr->val_handle);
    }

    return 0;
}

static int itag_on_service(uint16_t conn_handle,
                           const struct ble_gatt_error *error,
                           const struct ble_gatt_svc *svc,
                           void *arg)
{
    if (error->status == BLE_HS_EDONE)
    {
        ESP_LOGI(TAG, "Service discovery complete");
        return 0;
    }

    if (error->status != 0)
    {
        ESP_LOGE(TAG, "Service discovery error: %d", error->status);
        return error->status;
    }

    const ble_uuid_t *uuid = &svc->uuid.u;

    if (ble_uuid_cmp(uuid, BLE_UUID16_DECLARE(UUID_IMMEDIATE_ALERT)) == 0)
    {
        ESP_LOGI(TAG, "Found Immediate Alert service (start=0x%04x end=0x%04x)",
                 svc->start_handle, svc->end_handle);
        ble_gattc_disc_all_chrs(conn_handle,
                                svc->start_handle,
                                svc->end_handle,
                                itag_on_characteristic,
                                NULL);
    }
    else if (ble_uuid_cmp(uuid, BLE_UUID16_DECLARE(UUID_BATTERY_SERVICE)) == 0)
    {
        ESP_LOGI(TAG, "Found Battery service (start=0x%04x end=0x%04x)",
                 svc->start_handle, svc->end_handle);
        ble_gattc_disc_all_chrs(conn_handle,
                                svc->start_handle,
                                svc->end_handle,
                                itag_on_characteristic,
                                NULL);
    }
    else if (ble_uuid_cmp(uuid, BLE_UUID16_DECLARE(UUID_VENDOR_SERVICE)) == 0)
    {
        ESP_LOGI(TAG, "Found vendor service 0x%04x (start=0x%04x end=0x%04x)",
                 UUID_VENDOR_SERVICE, svc->start_handle, svc->end_handle);
        ble_gattc_disc_all_chrs(conn_handle,
                                svc->start_handle,
                                svc->end_handle,
                                itag_on_characteristic,
                                NULL);
    }

    return 0;
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
    {
        char addr_str[18];
        snprintf(addr_str, sizeof(addr_str),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 event->disc.addr.val[5], event->disc.addr.val[4],
                 event->disc.addr.val[3], event->disc.addr.val[2],
                 event->disc.addr.val[1], event->disc.addr.val[0]);

        bool is_scan_rsp = (event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP);

        if (event->disc.length_data == 0 || event->disc.data == NULL)
        {
            ESP_LOGI(TAG,
                     "%s (empty payload) addr=%s rssi=%ddBm type=0x%02X",
                     is_scan_rsp ? "SCAN_RSP" : "ADV",
                     addr_str,
                     event->disc.rssi,
                     event->disc.event_type);
            return 0;
        }

        struct ble_hs_adv_fields fields;
        bool parsed = (ble_hs_adv_parse_fields(&fields,
                                               event->disc.data,
                                               event->disc.length_data) == 0);
        if (!parsed)
        {
            ESP_LOGI(TAG, "%s (parse failed) addr=%s len=%u",
                     is_scan_rsp ? "SCAN_RSP" : "ADV",
                     addr_str,
                     event->disc.length_data);
            log_adv_payload(is_scan_rsp ? "SCAN_RSP" : "ADV",
                            addr_str,
                            event->disc.data,
                            event->disc.length_data);
        }

        char name_buf[32] = "<no name>";
        bool has_name = false;
        if (parsed && fields.name && fields.name_len > 0)
        {
            size_t copy_len = fields.name_len;
            if (copy_len >= sizeof(name_buf))
            {
                copy_len = sizeof(name_buf) - 1;
            }
            memcpy(name_buf, fields.name, copy_len);
            name_buf[copy_len] = '\0';
            has_name = true;
        }
        else if (extract_name_field(event->disc.data,
                                    event->disc.length_data,
                                    name_buf,
                                    sizeof(name_buf)))
        {
            has_name = true;
        }

        ESP_LOGI(TAG,
                 "%s addr=%s rssi=%ddBm type=0x%02X name=\"%s\"",
                 is_scan_rsp ? "SCAN_RSP" : "ADV",
                 addr_str,
                 event->disc.rssi,
                 event->disc.event_type,
                 name_buf);

        bool connectable = (event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND) ||
                           (event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) ||
                           is_scan_rsp;

        bool name_matches = false;
        if (has_name)
        {
            const char target[] = "itag";
            if (strlen(name_buf) >= sizeof(target) - 1)
            {
                name_matches = (strncasecmp(name_buf, target, sizeof(target) - 1) == 0);
            }
        }

        if (!s_connecting &&
            s_ctx.conn_handle == BLE_HS_CONN_HANDLE_NONE &&
            connectable &&
            name_matches)
        {
            ESP_LOGI(TAG, "Found iTag (%s), connecting...", addr_str);

            if (ble_gap_disc_cancel() != 0)
            {
                ESP_LOGW(TAG, "Failed to cancel scan before connect");
            }

            uint8_t own_addr_type;
            if (ble_hs_id_infer_auto(0, &own_addr_type) == 0 &&
                ble_gap_connect(own_addr_type,
                                &event->disc.addr,
                                30000,
                                NULL,
                                gap_event,
                                NULL) == 0)
            {
                s_connecting = true;
                return 0;
            }

            ESP_LOGW(TAG, "Immediate connect attempt failed; resuming scan");
            start_scan();
        }
        return 0;
    }

    case BLE_GAP_EVENT_CONNECT:
        s_connecting = false;
        if (event->connect.status == 0)
        {
            ESP_LOGI(TAG, "Connected (handle=%d)", event->connect.conn_handle);

            s_ctx.conn_handle = event->connect.conn_handle;
            s_ctx.initial_reads_requested = false;
            memset(s_ctx.chars, 0, sizeof(s_ctx.chars));

            int rc = ble_gattc_disc_all_svcs(s_ctx.conn_handle,
                                             itag_on_service,
                                             NULL);
            if (rc != 0)
            {
                ESP_LOGE(TAG, "Service discovery failed: %d", rc);
                ble_gap_terminate(s_ctx.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            }
        }
        else
        {
            ESP_LOGE(TAG, "Connection attempt failed (status=%d)", event->connect.status);
            start_scan();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected (reason=%d)", event->disconnect.reason);
        s_ctx.conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_ctx.initial_reads_requested = false;
        memset(s_ctx.chars, 0, sizeof(s_ctx.chars));
        s_connecting = false;
        start_scan();
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "Scan stopped (reason=%d)", event->disc_complete.reason);
        start_scan();
        return 0;

    default:
        ESP_LOGD(TAG, "Unhandled GAP event: %d", event->type);
        return 0;
    }
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

    start_scan();
}

static void host_task(void *param)
{
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void start_scan(void)
{
    uint8_t own_addr_type;
    struct ble_gap_disc_params params = {
        .passive = 0,
        .filter_duplicates = 0,
    };

    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0)
    {
        ESP_LOGE(TAG, "addr type inference failed");
        return;
    }

    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
    }
}

void init_ble_manager(void)
{
    ESP_ERROR_CHECK(nimble_port_init());

    if (s_alert_task == NULL)
    {
        BaseType_t created = xTaskCreate(itag_read_task,
                                         "itag_alert",
                                         2048,
                                         NULL,
                                         tskIDLE_PRIORITY + 1,
                                         &s_alert_task);
        if (created != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to create alert task");
            s_alert_task = NULL;
        }
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_svc_gap_device_name_set("esp32-itag-client");

    nimble_port_freertos_init(host_task);
}

void ble_manager_trigger_beep(uint8_t level)
{
    if (s_ctx.conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(TAG, "Cannot trigger alert: no active connection");
        return;
    }

    if (!s_ctx.chars[ITAG_CHAR_ALERT_LEVEL].present)
    {
        ESP_LOGW(TAG, "Cannot trigger alert: Alert Level characteristic not discovered");
        return;
    }

    int rc = ble_gattc_write_flat(s_ctx.conn_handle,
                                  s_ctx.chars[ITAG_CHAR_ALERT_LEVEL].value_handle,
                                  &level,
                                  sizeof(level),
                                  itag_write_cb,
                                  (void *)(uintptr_t)ITAG_CHAR_ALERT_LEVEL);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "Alert write failed: %d", rc);
    }
}

void ble_manager_read_battery_level()
{
    if (s_ctx.conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGW(TAG, "Cannot read battery: no active connection");
        return;
    }

    if (!s_ctx.chars[ITAG_CHAR_BATTERY_LEVEL].present)
    {
        ESP_LOGW(TAG, "Cannot read battery: Battery Level characteristic not discovered");
        return;
    }

    int rc = ble_gattc_read_by_uuid(s_ctx.conn_handle,
                                    s_ctx.chars[ITAG_CHAR_BATTERY_LEVEL].value_handle,
                                    s_ctx.chars[ITAG_CHAR_BATTERY_LEVEL].value_handle,
                                    BLE_UUID16_DECLARE(UUID_BATTERY_LEVEL),
                                    itag_read_cb,
                                    (void *)(uintptr_t)ITAG_CHAR_BATTERY_LEVEL);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "Battery read failed: %d", rc);
    }
}