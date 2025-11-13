#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"

#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "ble_manager.h"

static const char *TAG = "ble_manager";

#define MAX_REGISTERED_DEVICES 2

static const ble_device_driver_t *s_registered_devices[MAX_REGISTERED_DEVICES];
static int s_num_devices = 0;
static bool s_connecting = false;
static uint16_t s_active_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static const ble_device_driver_t *s_active_device = NULL;

int ble_manager_on_char(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        const struct ble_gatt_chr *chr,
                        void *arg);

typedef struct
{
    ble_svc_config_t *svc;
    EventGroupHandle_t event_group;
} ble_svc_arg_t;

int ble_manager_register_device(const ble_device_driver_t *driver)
{
    if (driver == NULL || driver->name == NULL)
    {
        return -1;
    }

    if (s_num_devices >= MAX_REGISTERED_DEVICES)
    {
        ESP_LOGE(TAG, "Maximum number of devices reached");
        return -1;
    }

    s_registered_devices[s_num_devices++] = driver;
    ESP_LOGI(TAG, "Registered device: %s", driver->name);
    return 0;
}

static bool match_device_by_services(const ble_device_driver_t *device, const struct ble_hs_adv_fields *fields)
{
    if (!fields)
    {
        return false;
    }

    if (fields->uuids16 != NULL && fields->num_uuids16 > 0)
    {
        for (int i = 0; i < device->config->n_services; i++)
        {
            uint16_t expected_uuid = device->config->services[i].uuid;
            for (int j = 0; j < fields->num_uuids16; j++)
            {
                if (fields->uuids16[j].value == expected_uuid)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

static void normalize_name(char *dst, const char *src, size_t dst_size)
{
    if (!dst || !src || dst_size == 0)
    {
        return;
    }

    size_t i = 0;
    size_t j = 0;
    while (src[i] != '\0' && j < dst_size - 1)
    {
        if (src[i] != ' ')
        {
            dst[j++] = (char)tolower((unsigned char)src[i]);
        }
        i++;
    }
    dst[j] = '\0';
}

static bool match_device_by_name(const ble_device_driver_t *device, const char *name)
{
    if (!device || !device->config || !name)
    {
        return false;
    }

    if (!device->config->name)
    {
        return false;
    }

    char normalized_device[32];
    char normalized_name[32];

    normalize_name(normalized_device, device->config->name, sizeof(normalized_device));
    normalize_name(normalized_name, name, sizeof(normalized_name));

    return strcmp(normalized_device, normalized_name) == 0;
}

static bool extract_name_from_raw(const uint8_t *data, uint16_t len, char *name_buf, size_t name_buf_size)
{
    if (!data || len == 0 || !name_buf || name_buf_size == 0)
    {
        return false;
    }

    const uint8_t *p = data;
    const uint8_t *end = data + len;

    while (p < end)
    {
        if (p + 1 >= end)
        {
            break;
        }

        uint8_t field_len = p[0];
        if (field_len == 0 || p + 1 + field_len > end)
        {
            break;
        }

        uint8_t field_type = p[1];

        if (field_type == 0x08 || field_type == 0x09)
        {
            uint8_t name_len = field_len - 1;
            if (name_len > 0 && name_len < name_buf_size)
            {
                memcpy(name_buf, p + 2, name_len);
                name_buf[name_len] = '\0';
                return true;
            }
        }

        p += 1 + field_len;
    }

    return false;
}

int ble_manager_on_svc(uint16_t conn_handle,
                       const struct ble_gatt_error *error,
                       const struct ble_gatt_svc *svc,
                       void *arg)
{
    ble_device_config_t *ctx = (ble_device_config_t *)arg;

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
    uint16_t service_uuid = 0;

    if (uuid->type == BLE_UUID_TYPE_16)
    {
        service_uuid = BLE_UUID16(uuid)->value;
    }
    else
    {
        ESP_LOGW(TAG, "Service UUID is not 16-bit (type=%d)", uuid->type);
        return 0;
    }

    bool found_in_config = false;
    for (int i = 0; i < ctx->n_services; i++)
    {
        if (ctx->services[i].uuid == service_uuid)
        {
            found_in_config = true;
            ESP_LOGI(TAG, "Found service 0x%04x (start=0x%04x end=0x%04x)",
                     service_uuid, svc->start_handle, svc->end_handle);

            ble_svc_arg_t *arg = malloc(sizeof(ble_svc_arg_t));
            if (arg == NULL)
            {
                ESP_LOGE(TAG, "Failed to allocate memory for service arg");
                return 0;
            }
            arg->svc = &ctx->services[i];
            arg->event_group = ctx->event_group;
            ble_gattc_disc_all_chrs(conn_handle,
                                    svc->start_handle,
                                    svc->end_handle,
                                    ble_manager_on_char, arg);
            break;
        }
    }

    if (!found_in_config)
    {
        ESP_LOGI(TAG, "Found vendor service 0x%04x (start=0x%04x end=0x%04x) - not in config",
                 service_uuid, svc->start_handle, svc->end_handle);
    }

    return 0;
}

int ble_manager_on_char(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        const struct ble_gatt_chr *chr,
                        void *arg)
{
    ble_svc_arg_t *ctx = (ble_svc_arg_t *)arg;
    ble_svc_config_t *svc = ctx->svc;

    if (error->status == BLE_HS_EDONE)
    {
        ESP_LOGI(TAG, "Characteristic discovery complete");
        free(arg);
        return 0;
    }

    if (error->status != 0)
    {
        ESP_LOGE(TAG, "Characteristic discovery error: %d", error->status);
        free(arg);
        return error->status;
    }

    const ble_uuid_t *uuid = &chr->uuid.u;
    uint16_t char_uuid = 0;

    if (uuid->type == BLE_UUID_TYPE_16)
    {
        char_uuid = BLE_UUID16(uuid)->value;
    }
    else
    {
        ESP_LOGW(TAG, "Characteristic UUID is not 16-bit (type=%d)", uuid->type);
        return 0;
    }

    bool found_in_config = false;
    for (int i = 0; i < svc->n_chars; i++)
    {
        if (svc->chars[i].uuid == char_uuid)
        {
            found_in_config = true;
            svc->chars[i].handle = chr->val_handle;
            ESP_LOGI(TAG, "Found characteristic 0x%04x (service 0x%04x) handle=0x%04x",
                     char_uuid, svc->uuid, chr->val_handle);
            xEventGroupSetBits(ctx->event_group, svc->chars[i].bit);
            break;
        }
    }

    if (!found_in_config)
    {
        ESP_LOGI(TAG, "Found vendor characteristic 0x%04x (service 0x%04x) handle=0x%04x - not in config",
                 char_uuid, svc->uuid, chr->val_handle);
    }

    return 0;
}

static int ble_manager_on_read(uint16_t conn_handle,
                               const struct ble_gatt_error *error,
                               struct ble_gatt_attr *attr,
                               void *arg)
{
    ble_char_config_t *char_config = (ble_char_config_t *)arg;

    if (char_config->read_cb)
    {
        if (error->status == 0 && attr != NULL && attr->om != NULL)
        {
            char_config->read_cb(attr->om->om_data, attr->om->om_len, 0);
        }
        else
        {
            char_config->read_cb(NULL, 0, error->status != 0 ? error->status : -1);
        }
    }

    return 0;
}

static int ble_manager_on_write(uint16_t conn_handle,
                                const struct ble_gatt_error *error,
                                struct ble_gatt_attr *attr,
                                void *arg)
{
    ble_char_config_t *char_config = (ble_char_config_t *)arg;

    ESP_LOGI(TAG, "ble_manager_on_write called: status=%d, char_uuid=0x%04x, write_cb=%p",
             error->status, char_config ? char_config->uuid : 0, char_config ? char_config->write_cb : NULL);

    if (char_config && char_config->write_cb)
    {
        char_config->write_cb(error->status);
    }
    else if (char_config)
    {
        ESP_LOGI(TAG, "CCCD write completed for characteristic 0x%04x (status=%d)", char_config->uuid, error->status);
    }

    return 0;
}

int ble_manager_read_char(ble_char_config_t *char_config)
{
    if (!char_config || !s_active_device || !s_active_device->config || s_active_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        return -1;
    }

    if (char_config->handle == 0)
    {
        ESP_LOGE(TAG, "Characteristic 0x%04x not discovered", char_config->uuid);
        return -1;
    }

    int rc = ble_gattc_read(s_active_conn_handle, char_config->handle, ble_manager_on_read, char_config);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to initiate read: %d", rc);
        return rc;
    }

    return 0;
}

int ble_manager_write_char(ble_char_config_t *char_config, const uint8_t *data, uint16_t len)
{
    if (!char_config || !s_active_device || !s_active_device->config || s_active_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        return -1;
    }

    if (char_config->handle == 0)
    {
        ESP_LOGE(TAG, "Characteristic 0x%04x not discovered", char_config->uuid);
        return -1;
    }

    int rc = ble_gattc_write_flat(s_active_conn_handle, char_config->handle, data, len, ble_manager_on_write, char_config);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to initiate write: %d", rc);
        return rc;
    }

    return 0;
}

static void start_scan(void);

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
            if (event->disc.data != NULL && event->disc.length_data > 0)
            {
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, event->disc.data, event->disc.length_data, ESP_LOG_INFO);
            }
        }

        char name_buf[32] = "<no name>";
        if (parsed && fields.name && fields.name_len > 0)
        {
            size_t copy_len = fields.name_len;
            if (copy_len >= sizeof(name_buf))
            {
                copy_len = sizeof(name_buf) - 1;
            }
            memcpy(name_buf, fields.name, copy_len);
            name_buf[copy_len] = '\0';
        }
        else if (!parsed && event->disc.data && event->disc.length_data > 0)
        {
            extract_name_from_raw(event->disc.data, event->disc.length_data, name_buf, sizeof(name_buf));
        }

        char uuid_str[128] = "";
        if (parsed && fields.uuids16 != NULL && fields.num_uuids16 > 0)
        {
            char *p = uuid_str;
            for (int i = 0; i < fields.num_uuids16 && i < 8; i++)
            {
                p += snprintf(p, sizeof(uuid_str) - (p - uuid_str), "0x%04X ", fields.uuids16[i].value);
            }
        }

        ESP_LOGI(TAG,
                 "%s addr=%s rssi=%ddBm type=0x%02X name=\"%s\" UUIDs=[%s]",
                 is_scan_rsp ? "SCAN_RSP" : "ADV",
                 addr_str,
                 event->disc.rssi,
                 event->disc.event_type,
                 name_buf,
                 uuid_str[0] ? uuid_str : "<none>");

        bool connectable = (event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND) ||
                           (event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) ||
                           is_scan_rsp;

        if (!s_connecting && s_active_conn_handle == BLE_HS_CONN_HANDLE_NONE && connectable)
        {
            for (int i = 0; i < s_num_devices; i++)
            {
                bool matched = false;
                if (parsed)
                {
                    matched = match_device_by_services(s_registered_devices[i], &fields);
                }

                if (!matched && strcmp(name_buf, "<no name>") != 0)
                {
                    matched = match_device_by_name(s_registered_devices[i], name_buf);
                }

                if (matched)
                {
                    ESP_LOGI(TAG, "Found %s (%s), connecting...",
                             s_registered_devices[i]->name, addr_str);

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
                                        (void *)s_registered_devices[i]) == 0)
                    {
                        s_connecting = true;
                        return 0;
                    }

                    ESP_LOGW(TAG, "Immediate connect attempt failed; resuming scan");
                    break;
                }
            }
        }
        return 0;
    }

    case BLE_GAP_EVENT_CONNECT:
    {
        s_connecting = false;
        const ble_device_driver_t *device = (const ble_device_driver_t *)arg;

        if (event->connect.status == 0)
        {
            s_active_conn_handle = event->connect.conn_handle;
            s_active_device = device;

            if (device && device->config)
            {
                ESP_LOGI(TAG, "[%s] Connected (handle=%d)", device->name, event->connect.conn_handle);
                xEventGroupSetBits(device->config->event_group, device->config->bit_connected);

                int rc = ble_gattc_disc_all_svcs(event->connect.conn_handle,
                                                 ble_manager_on_svc,
                                                 (void *)device->config);
                if (rc != 0)
                {
                    ESP_LOGE(TAG, "[%s] Service discovery failed: %d", device->name, rc);
                }
            }
        }
        else
        {
            ESP_LOGE(TAG, "Connection attempt failed (status=%d)", event->connect.status);
            start_scan();
        }
        return 0;
    }

    case BLE_GAP_EVENT_DISCONNECT:
    {
        if (s_active_device)
        {
            ESP_LOGI(TAG, "[%s] Disconnected (reason=0x%02x)", s_active_device->name, event->disconnect.reason);

            for (int i = 0; i < s_active_device->config->n_services; i++)
            {
                for (int j = 0; j < s_active_device->config->services[i].n_chars; j++)
                {
                    s_active_device->config->services[i].chars[j].handle = 0;
                }
            }

            xEventGroupClearBits(s_active_device->config->event_group,
                                 s_active_device->config->bit_connected);
        }
        else
        {
            ESP_LOGI(TAG, "Disconnected (reason=0x%02x)", event->disconnect.reason);
        }

        s_active_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_active_device = NULL;
        start_scan();
        return 0;
    }

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
        .passive = 1,
        .filter_duplicates = 1,
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

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_svc_gap_device_name_set("esp32-ble-client");

    nimble_port_freertos_init(host_task);
}