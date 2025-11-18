#include "sdkconfig.h"

#include <stddef.h>
#include <ctype.h>
#include <string.h>

#ifndef CONFIG_LOG_MAXIMUM_LEVEL
#define CONFIG_LOG_MAXIMUM_LEVEL 3
#endif

#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

#ifndef BLE_HS_EDONE
#define BLE_HS_EDONE 0x0200
#endif

static const char *tag = "nimble_gatt_client";
static const char *target_device_name = "aquatest";

static const ble_uuid16_t battery_svc_uuid = BLE_UUID16_INIT(0x180F);
static const ble_uuid16_t battery_chr_uuid = BLE_UUID16_INIT(0x2A19);
static const ble_uuid16_t tx_power_svc_uuid = BLE_UUID16_INIT(0x1804);
static const ble_uuid16_t tx_power_chr_uuid = BLE_UUID16_INIT(0x2A07);
static const ble_uuid16_t immediate_alert_svc_uuid = BLE_UUID16_INIT(0x1802);
static const ble_uuid16_t alert_level_chr_uuid = BLE_UUID16_INIT(0x2A06);

struct blecent_conn_state
{
    uint16_t conn_handle;
    uint16_t battery_start_handle;
    uint16_t battery_end_handle;
    uint16_t battery_chr_val_handle;
    uint16_t tx_power_start_handle;
    uint16_t tx_power_end_handle;
    uint16_t tx_power_chr_val_handle;
    uint16_t immediate_alert_start_handle;
    uint16_t immediate_alert_end_handle;
    uint16_t alert_level_chr_val_handle;
};

static struct blecent_conn_state g_conn_state = {
    .conn_handle = BLE_HS_CONN_HANDLE_NONE,
};

static int blecent_gap_event(struct ble_gap_event *event, void *arg);

#define BLECENT_MAX_NAME_LEN 32

void ble_store_config_init(void);

static void
normalize_name(const uint8_t *src, uint8_t src_len, char *dst, size_t dst_len)
{
    size_t copy_len;

    if (dst_len == 0)
    {
        return;
    }

    copy_len = src_len < (dst_len - 1) ? src_len : (dst_len - 1);
    for (size_t i = 0; i < copy_len; i++)
    {
        dst[i] = (char)tolower((int)src[i]);
    }
    dst[copy_len] = '\0';
}

static void blecent_reset_conn_state(void);
static void blecent_start_battery_service_disc(uint16_t conn_handle);
static int blecent_on_battery_svc(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  const struct ble_gatt_svc *service,
                                  void *arg);
static void blecent_start_battery_chr_disc(uint16_t conn_handle);
static int blecent_on_battery_chr(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  const struct ble_gatt_chr *chr,
                                  void *arg);
static void blecent_start_tx_power_service_disc(uint16_t conn_handle);
static int blecent_on_tx_power_svc(uint16_t conn_handle,
                                   const struct ble_gatt_error *error,
                                   const struct ble_gatt_svc *service,
                                   void *arg);
static void blecent_start_tx_power_chr_disc(uint16_t conn_handle);
static int blecent_on_tx_power_chr(uint16_t conn_handle,
                                   const struct ble_gatt_error *error,
                                   const struct ble_gatt_chr *chr,
                                   void *arg);
static void blecent_start_immediate_alert_service_disc(uint16_t conn_handle);
static int blecent_on_immediate_alert_svc(uint16_t conn_handle,
                                          const struct ble_gatt_error *error,
                                          const struct ble_gatt_svc *service,
                                          void *arg);
static void blecent_start_alert_level_chr_disc(uint16_t conn_handle);
static int blecent_on_alert_level_chr(uint16_t conn_handle,
                                      const struct ble_gatt_error *error,
                                      const struct ble_gatt_chr *chr,
                                      void *arg);

static void
blecent_scan(void)
{
    uint8_t own_addr_type;
    struct ble_gap_disc_params disc_params = {0};
    int rc;

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(tag, "error determining address type; rc=%d", rc);
        return;
    }

    disc_params.filter_duplicates = 1;
    disc_params.passive = 1;

    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
                      blecent_gap_event, NULL);
    if (rc != 0)
    {
        ESP_LOGE(tag, "Error initiating GAP discovery procedure; rc=%d", rc);
    }
}

static void
blecent_reset_conn_state(void)
{
    memset(&g_conn_state, 0, sizeof(g_conn_state));
    g_conn_state.conn_handle = BLE_HS_CONN_HANDLE_NONE;
}

static void
blecent_start_battery_service_disc(uint16_t conn_handle)
{
    int rc = ble_gattc_disc_svc_by_uuid(conn_handle, &battery_svc_uuid.u,
                                        blecent_on_battery_svc, NULL);
    if (rc != 0)
    {
        ESP_LOGE(tag, "Battery service discovery start failed; rc=%d", rc);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static int
blecent_on_battery_svc(uint16_t conn_handle,
                       const struct ble_gatt_error *error,
                       const struct ble_gatt_svc *service,
                       void *arg)
{
    if (error->status == 0)
    {
        g_conn_state.battery_start_handle = service->start_handle;
        g_conn_state.battery_end_handle = service->end_handle;
        ESP_LOGI(tag, "Battery service discovered (start=0x%04X end=0x%04X)",
                 service->start_handle, service->end_handle);
        return 0;
    }

    if (error->status == BLE_HS_EDONE)
    {
        blecent_start_battery_chr_disc(conn_handle);
        return 0;
    }

    ESP_LOGE(tag, "Battery service discovery failed; status=%d",
             error->status);
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return error->status;
}

static void
blecent_start_battery_chr_disc(uint16_t conn_handle)
{
    int rc;

    if (g_conn_state.battery_start_handle == 0)
    {
        ESP_LOGW(tag, "Battery service not found on peer");
        blecent_start_tx_power_service_disc(conn_handle);
        return;
    }

    rc = ble_gattc_disc_chrs_by_uuid(conn_handle,
                                     g_conn_state.battery_start_handle,
                                     g_conn_state.battery_end_handle,
                                     &battery_chr_uuid.u,
                                     blecent_on_battery_chr, NULL);
    if (rc != 0)
    {
        ESP_LOGE(tag, "Battery Level characteristic discovery start failed; rc=%d",
                 rc);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static int
blecent_on_battery_chr(uint16_t conn_handle,
                       const struct ble_gatt_error *error,
                       const struct ble_gatt_chr *chr,
                       void *arg)
{
    if (error->status == 0)
    {
        g_conn_state.battery_chr_val_handle = chr->val_handle;
        ESP_LOGI(tag, "Battery Level characteristic discovered (handle=0x%04X)",
                 chr->val_handle);
        return 0;
    }

    if (error->status == BLE_HS_EDONE)
    {
        blecent_start_tx_power_service_disc(conn_handle);
        return 0;
    }

    ESP_LOGE(tag, "Battery Level characteristic discovery failed; status=%d",
             error->status);
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return error->status;
}

static void
blecent_start_tx_power_service_disc(uint16_t conn_handle)
{
    int rc = ble_gattc_disc_svc_by_uuid(conn_handle, &tx_power_svc_uuid.u,
                                        blecent_on_tx_power_svc, NULL);
    if (rc != 0)
    {
        ESP_LOGE(tag, "Tx Power service discovery start failed; rc=%d", rc);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static int
blecent_on_tx_power_svc(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        const struct ble_gatt_svc *service,
                        void *arg)
{
    if (error->status == 0)
    {
        g_conn_state.tx_power_start_handle = service->start_handle;
        g_conn_state.tx_power_end_handle = service->end_handle;
        ESP_LOGI(tag, "Tx Power service discovered (start=0x%04X end=0x%04X)",
                 service->start_handle, service->end_handle);
        return 0;
    }

    if (error->status == BLE_HS_EDONE)
    {
        blecent_start_tx_power_chr_disc(conn_handle);
        return 0;
    }

    ESP_LOGE(tag, "Tx Power service discovery failed; status=%d",
             error->status);
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return error->status;
}

static void
blecent_start_tx_power_chr_disc(uint16_t conn_handle)
{
    int rc;

    if (g_conn_state.tx_power_start_handle == 0)
    {
        ESP_LOGW(tag, "Tx Power service not found on peer");
        blecent_start_immediate_alert_service_disc(conn_handle);
        return;
    }

    rc = ble_gattc_disc_chrs_by_uuid(conn_handle,
                                     g_conn_state.tx_power_start_handle,
                                     g_conn_state.tx_power_end_handle,
                                     &tx_power_chr_uuid.u,
                                     blecent_on_tx_power_chr, NULL);
    if (rc != 0)
    {
        ESP_LOGE(tag, "Tx Power Level characteristic discovery start failed; rc=%d", rc);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static int
blecent_on_tx_power_chr(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        const struct ble_gatt_chr *chr,
                        void *arg)
{
    if (error->status == 0)
    {
        g_conn_state.tx_power_chr_val_handle = chr->val_handle;
        ESP_LOGI(tag, "Tx Power Level characteristic discovered (handle=0x%04X)",
                 chr->val_handle);
        return 0;
    }

    if (error->status == BLE_HS_EDONE)
    {
        blecent_start_immediate_alert_service_disc(conn_handle);
        return 0;
    }

    ESP_LOGE(tag, "Tx Power Level characteristic discovery failed; status=%d",
             error->status);
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return error->status;
}

static void
blecent_start_immediate_alert_service_disc(uint16_t conn_handle)
{
    int rc = ble_gattc_disc_svc_by_uuid(conn_handle, &immediate_alert_svc_uuid.u,
                                        blecent_on_immediate_alert_svc, NULL);
    if (rc != 0)
    {
        ESP_LOGE(tag, "Immediate Alert service discovery start failed; rc=%d", rc);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static int
blecent_on_immediate_alert_svc(uint16_t conn_handle,
                               const struct ble_gatt_error *error,
                               const struct ble_gatt_svc *service,
                               void *arg)
{
    if (error->status == 0)
    {
        g_conn_state.immediate_alert_start_handle = service->start_handle;
        g_conn_state.immediate_alert_end_handle = service->end_handle;
        ESP_LOGI(tag, "Immediate Alert service discovered (start=0x%04X end=0x%04X)",
                 service->start_handle, service->end_handle);
        return 0;
    }

    if (error->status == BLE_HS_EDONE)
    {
        blecent_start_alert_level_chr_disc(conn_handle);
        return 0;
    }

    ESP_LOGE(tag, "Immediate Alert service discovery failed; status=%d",
             error->status);
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return error->status;
}

static void
blecent_start_alert_level_chr_disc(uint16_t conn_handle)
{
    int rc;

    if (g_conn_state.immediate_alert_start_handle == 0)
    {
        ESP_LOGW(tag, "Immediate Alert service not found on peer");
        return;
    }

    rc = ble_gattc_disc_chrs_by_uuid(conn_handle,
                                     g_conn_state.immediate_alert_start_handle,
                                     g_conn_state.immediate_alert_end_handle,
                                     &alert_level_chr_uuid.u,
                                     blecent_on_alert_level_chr, NULL);
    if (rc != 0)
    {
        ESP_LOGE(tag, "Alert Level characteristic discovery start failed; rc=%d", rc);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static int blecent_on_battery_read(uint16_t conn_handle,
                                   const struct ble_gatt_error *error,
                                   struct ble_gatt_attr *attr,
                                   void *arg);
static int blecent_on_alert_level_write(uint16_t conn_handle,
                                        const struct ble_gatt_error *error,
                                        struct ble_gatt_attr *attr,
                                        void *arg);
static int blecent_on_tx_power_subscribe(uint16_t conn_handle,
                                         const struct ble_gatt_error *error,
                                         struct ble_gatt_attr *attr,
                                         void *arg);

static int
blecent_on_alert_level_chr(uint16_t conn_handle,
                           const struct ble_gatt_error *error,
                           const struct ble_gatt_chr *chr,
                           void *arg)
{
    if (error->status == 0)
    {
        g_conn_state.alert_level_chr_val_handle = chr->val_handle;
        ESP_LOGI(tag, "Alert Level characteristic discovered (handle=0x%04X)", chr->val_handle);
        return 0;
    }

    if (error->status == BLE_HS_EDONE)
    {
        ESP_LOGI(tag, "Discovery complete for connection 0x%04X", g_conn_state.conn_handle);
        return 0;
    }

    ESP_LOGE(tag, "Alert Level characteristic discovery failed; status=%d",
             error->status);
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return error->status;
}

static int blecent_read_battery(uint16_t conn_handle)
{
    if (g_conn_state.battery_chr_val_handle != 0)
    {
        int rc = ble_gattc_read(conn_handle, g_conn_state.battery_chr_val_handle,
                                blecent_on_battery_read, NULL);
        if (rc != 0)
        {
            ESP_LOGE(tag, "Failed to read battery level; rc=%d", rc);
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return rc;
        }
        return 0;
    }
    else
    {
        ESP_LOGW(tag, "Battery characteristic not found, skipping read");
        return BLE_HS_ENOENT;
    }
}

static int
blecent_on_battery_read(uint16_t conn_handle,
                        const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr,
                        void *arg)
{
    if (error->status == 0)
    {
        uint8_t battery_level = 0;
        if (attr->om != NULL && OS_MBUF_PKTLEN(attr->om) >= 1)
        {
            os_mbuf_copydata(attr->om, 0, 1, &battery_level);
            ESP_LOGI(tag, "Battery Level read: %d%% (handle=0x%04X)",
                     battery_level, attr->handle);
        }
        else
        {
            ESP_LOGW(tag, "Battery Level read: empty or invalid data");
        }
    }
    else
    {
        ESP_LOGE(tag, "Battery Level read failed; status=%d", error->status);
        return error->status;
    }

    return 0;
}

static int blecent_write_alert_level(uint16_t conn_handle, uint8_t alert_value)
{
    if (g_conn_state.alert_level_chr_val_handle != 0)
    {
        int rc = ble_gattc_write_flat(conn_handle, g_conn_state.alert_level_chr_val_handle,
                                      &alert_value, sizeof(alert_value),
                                      blecent_on_alert_level_write, NULL);
        if (rc != 0)
        {
            ESP_LOGE(tag, "Failed to write alert level; rc=%d", rc);
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return rc;
        }
        return 0;
    }
    else
    {
        ESP_LOGW(tag, "Alert Level characteristic not found, skipping write");
        return BLE_HS_ENOENT;
    }
}

static int
blecent_on_alert_level_write(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             struct ble_gatt_attr *attr,
                             void *arg)
{
    if (error->status == 0)
    {
        ESP_LOGI(tag, "Alert Level write successful (handle=0x%04X)", attr->handle);
    }
    else
    {
        ESP_LOGE(tag, "Alert Level write failed; status=%d", error->status);
        return error->status;
    }

    return 0;
}

static int blecent_subscribe_tx_power(uint16_t conn_handle, uint16_t value)
{
    if (g_conn_state.tx_power_chr_val_handle != 0)
    {
        uint16_t cccd_handle = g_conn_state.tx_power_chr_val_handle + 1; /* CCCD is typically next handle */
        uint8_t cccd_value[2] = {(uint8_t)(value & 0xFF), (uint8_t)(value >> 8)};
        int rc = ble_gattc_write_flat(conn_handle, cccd_handle,
                                      cccd_value, sizeof(cccd_value),
                                      blecent_on_tx_power_subscribe, NULL);
        if (rc != 0)
        {
            ESP_LOGW(tag, "Failed to write CCCD directly (handle=0x%04X), rc=%d", cccd_handle, rc);
            ESP_LOGI(tag, "Note: CCCD discovery may be needed for tx power subscription");
            return rc;
        }
        return 0;
    }
    else
    {
        ESP_LOGW(tag, "Tx Power characteristic not found, skipping subscribe");
        return BLE_HS_ENOENT;
    }
}

static int
blecent_on_tx_power_subscribe(uint16_t conn_handle,
                              const struct ble_gatt_error *error,
                              struct ble_gatt_attr *attr,
                              void *arg)
{
    if (error->status == 0)
    {
        ESP_LOGI(tag, "Tx Power subscription successful (handle=0x%04X)", attr->handle);
        ESP_LOGI(tag, "All operations complete: read battery, wrote alert, subscribed to tx power");
    }
    else
    {
        ESP_LOGE(tag, "Tx Power subscription failed; status=%d", error->status);
    }

    return 0;
}

static int
blecent_should_connect(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    char adv_name[BLECENT_MAX_NAME_LEN] = {0};
    int rc;

    if (disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
        disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND)
    {
        return 0;
    }

    rc = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
    if (rc != 0)
    {
        return 0;
    }

    if (fields.name_len == 0 || fields.name == NULL)
    {
        return 0;
    }

    normalize_name(fields.name, fields.name_len, adv_name, sizeof(adv_name));

    return strcmp(adv_name, target_device_name) == 0;
}

static void
blecent_connect_if_interesting(void *disc)
{
    uint8_t own_addr_type;
    int rc;
    ble_addr_t *addr;

    if (!blecent_should_connect((struct ble_gap_disc_desc *)disc))
    {
        return;
    }

#if !(MYNEWT_VAL(BLE_HOST_ALLOW_CONNECT_WITH_SCAN))
    rc = ble_gap_disc_cancel();
    if (rc != 0)
    {
        ESP_LOGD(tag, "Failed to cancel scan; rc=%d", rc);
        return;
    }
#endif

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(tag, "error determining address type; rc=%d", rc);
        return;
    }

    addr = &((struct ble_gap_disc_desc *)disc)->addr;

    rc = ble_gap_connect(own_addr_type, addr, 30000, NULL,
                         blecent_gap_event, NULL);
    if (rc != 0)
    {
        return;
    }
}

static int
blecent_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
        blecent_connect_if_interesting(&event->disc);
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            ESP_LOGI(tag, "Connection established");
            blecent_reset_conn_state();
            g_conn_state.conn_handle = event->connect.conn_handle;
            blecent_start_battery_service_disc(event->connect.conn_handle);
        }
        else
        {
            ESP_LOGE(tag, "Connection failed; status=%d",
                     event->connect.status);
            blecent_scan();
        }

        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(tag, "disconnect; reason=%d", event->disconnect.reason);
        blecent_reset_conn_state();
        blecent_scan();
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(tag, "discovery complete; reason=%d",
                 event->disc_complete.reason);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.attr_handle == g_conn_state.tx_power_chr_val_handle)
        {
            int8_t tx_power = 0;
            if (event->notify_rx.om != NULL && OS_MBUF_PKTLEN(event->notify_rx.om) >= 1)
            {
                os_mbuf_copydata(event->notify_rx.om, 0, 1, &tx_power);
                ESP_LOGI(tag, "Tx Power notification received: %d dBm (handle=0x%04X)",
                         tx_power, event->notify_rx.attr_handle);
            }
        }
        else
        {
            ESP_LOGI(tag, "Notification received; conn_handle=%d attr_handle=0x%04X len=%d",
                     event->notify_rx.conn_handle,
                     event->notify_rx.attr_handle,
                     OS_MBUF_PKTLEN(event->notify_rx.om));
        }
        return 0;

    default:
        return 0;
    }
}

static void
blecent_on_reset(int reason)
{
    ESP_LOGE(tag, "Resetting state; reason=%d", reason);
}

static void
blecent_on_sync(void)
{
    int rc;

    /* Make sure we have proper identity address set (public preferred) */
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* Begin scanning for a peripheral to connect to. */
    blecent_scan();
}

void blecent_host_task(void *param)
{
    ESP_LOGI(tag, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}

int gatt_client_read_battery(void)
{
    if (g_conn_state.conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGE(tag, "Not connected to any device");
        return BLE_HS_ENOTCONN;
    }
    return blecent_read_battery(g_conn_state.conn_handle);
}

int gatt_client_write_alert_level(uint8_t level)
{
    if (g_conn_state.conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGE(tag, "Not connected to any device");
        return BLE_HS_ENOTCONN;
    }
    if (level > 2)
    {
        ESP_LOGE(tag, "Invalid alert level: %d (must be 0, 1, or 2)", level);
        return BLE_HS_EINVAL;
    }
    return blecent_write_alert_level(g_conn_state.conn_handle, level);
}

int gatt_client_set_notifications(uint8_t enable)
{
    if (g_conn_state.conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGE(tag, "Not connected to any device");
        return BLE_HS_ENOTCONN;
    }
    uint16_t value = enable ? 0x0001 : 0x0000; /* Enable/disable notifications */
    return blecent_subscribe_tx_power(g_conn_state.conn_handle, value);
}

void start_gatt_client(void)
{
    int rc;
    blecent_reset_conn_state();
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = nimble_port_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(tag, "Failed to init nimble %d ", ret);
        return;
    }

    ble_hs_cfg.reset_cb = blecent_on_reset;
    ble_hs_cfg.sync_cb = blecent_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

#if CONFIG_BT_NIMBLE_GAP_SERVICE
    /* Set the default device name. */
    rc = ble_svc_gap_device_name_set("nimble-blecent");
    assert(rc == 0);
#endif

    /* XXX Need to have template for store */
    ble_store_config_init();

    nimble_port_freertos_init(blecent_host_task);
}