#include "sdkconfig.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
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
#include "keyboard_simulator.h"

static const char *tag = "NimBLE_BLE_CENT";
static const char *target_device_name = "popicon";

static const ble_uuid16_t gap_svc_uuid = BLE_UUID16_INIT(0x1800);
static const ble_uuid16_t battery_svc_uuid = BLE_UUID16_INIT(0x180F);
static const ble_uuid16_t battery_chr_uuid = BLE_UUID16_INIT(0x2A19);
static const ble_uuid16_t hid_svc_uuid = BLE_UUID16_INIT(0x1812);

#define MAX_REPORT_CHARS 8

struct blecent_conn_state
{
    uint16_t conn_handle;
    uint16_t gap_start_handle;
    uint16_t gap_end_handle;
    uint16_t gap_device_name_chr_val_handle;
    uint16_t gap_appearance_chr_val_handle;
    uint16_t gap_ppcp_chr_val_handle;
    uint16_t battery_start_handle;
    uint16_t battery_end_handle;
    uint16_t battery_chr_val_handle;
    uint16_t hid_start_handle;
    uint16_t hid_end_handle;

    uint16_t hid_info_chr_val_handle;           /* 0x2A4A */
    uint16_t hid_boot_kb_input_chr_val_handle;  /* 0x2A22 - for boot protocol notifications */
    uint16_t hid_boot_kb_input_cccd_handle;     /* CCCD for 0x2A22 */
    uint16_t hid_boot_kb_output_chr_val_handle; /* 0x2A32 */
    uint16_t hid_report_map_chr_val_handle;     /* 0x2A4B */

    uint16_t hid_report_chr_val_handles[MAX_REPORT_CHARS];
    uint16_t hid_report_cccd_handles[MAX_REPORT_CHARS];
    uint8_t hid_report_count;                  /* Number of Report characteristics found */
    uint16_t hid_control_point_chr_val_handle; /* 0x2A4C */
    uint16_t hid_protocol_mode_chr_val_handle; /* 0x2A4E */
    uint8_t current_protocol_mode;             /* 0 = Boot, 1 = Report (default to Report) */
};

static struct blecent_conn_state g_conn_state = {
    .conn_handle = BLE_HS_CONN_HANDLE_NONE,
    .current_protocol_mode = 1,
};

static int blecent_gap_event(struct ble_gap_event *event, void *arg);

#define BLECENT_MAX_NAME_LEN 32

void ble_store_config_init(void);

static void
normalize_name(const uint8_t *src, uint8_t src_len, char *dst, size_t dst_len)
{
    size_t dst_idx = 0;

    if (dst_len == 0)
    {
        return;
    }

    for (size_t i = 0; i < src_len && dst_idx < (dst_len - 1); i++)
    {
        char c = (char)tolower((int)src[i]);
        if (c != ' ')
        {
            dst[dst_idx++] = c;
        }
    }
    dst[dst_idx] = '\0';
}

static void blecent_reset_conn_state(void);
static void blecent_start_gap_service_disc(uint16_t conn_handle);
static int blecent_on_gap_svc(uint16_t conn_handle,
                              const struct ble_gatt_error *error,
                              const struct ble_gatt_svc *service,
                              void *arg);
static void blecent_start_gap_chr_disc(uint16_t conn_handle);
static int blecent_on_gap_chr(uint16_t conn_handle,
                              const struct ble_gatt_error *error,
                              const struct ble_gatt_chr *chr,
                              void *arg);
static int blecent_on_device_name_read(uint16_t conn_handle,
                                       const struct ble_gatt_error *error,
                                       struct ble_gatt_attr *attr,
                                       void *arg);
static int blecent_on_device_name_write(uint16_t conn_handle,
                                        const struct ble_gatt_error *error,
                                        struct ble_gatt_attr *attr,
                                        void *arg);
static int blecent_on_appearance_read(uint16_t conn_handle,
                                      const struct ble_gatt_error *error,
                                      struct ble_gatt_attr *attr,
                                      void *arg);
static int blecent_on_ppcp_read(uint16_t conn_handle,
                                const struct ble_gatt_error *error,
                                struct ble_gatt_attr *attr,
                                void *arg);
static int blecent_read_device_name(uint16_t conn_handle);
static int blecent_write_device_name(uint16_t conn_handle, const char *name);
static int blecent_read_appearance(uint16_t conn_handle);
static int blecent_read_ppcp(uint16_t conn_handle);
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
static void blecent_start_hid_service_disc(uint16_t conn_handle);
static int blecent_on_hid_svc(uint16_t conn_handle,
                              const struct ble_gatt_error *error,
                              const struct ble_gatt_svc *service,
                              void *arg);
static void blecent_start_hid_chr_disc(uint16_t conn_handle);
static int blecent_on_hid_chr(uint16_t conn_handle,
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
    g_conn_state.current_protocol_mode = 1; /* Default to Report Protocol */
}

static void
blecent_start_gap_service_disc(uint16_t conn_handle)
{
    int rc = ble_gattc_disc_svc_by_uuid(conn_handle, &gap_svc_uuid.u,
                                        blecent_on_gap_svc, NULL);
    if (rc != 0)
    {
        ESP_LOGE(tag, "GAP service discovery start failed; rc=%d", rc);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static int
blecent_on_gap_svc(uint16_t conn_handle,
                   const struct ble_gatt_error *error,
                   const struct ble_gatt_svc *service,
                   void *arg)
{
    if (error->status == 0)
    {
        g_conn_state.gap_start_handle = service->start_handle;
        g_conn_state.gap_end_handle = service->end_handle;
        ESP_LOGI(tag, "GAP service discovered (start=0x%04X end=0x%04X)",
                 service->start_handle, service->end_handle);
        return 0;
    }

    if (error->status == BLE_HS_EDONE)
    {
        blecent_start_gap_chr_disc(conn_handle);
        return 0;
    }

    ESP_LOGE(tag, "GAP service discovery failed; status=%d", error->status);
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return error->status;
}

static void
blecent_start_gap_chr_disc(uint16_t conn_handle)
{
    int rc;

    if (g_conn_state.gap_start_handle == 0)
    {
        ESP_LOGW(tag, "GAP service not found on peer");
        blecent_start_battery_service_disc(conn_handle);
        return;
    }

    rc = ble_gattc_disc_all_chrs(conn_handle, g_conn_state.gap_start_handle, g_conn_state.gap_end_handle, blecent_on_gap_chr, NULL);
    if (rc != 0)
    {
        ESP_LOGE(tag, "GAP characteristic discovery start failed; rc=%d", rc);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static int
blecent_on_gap_chr(uint16_t conn_handle,
                   const struct ble_gatt_error *error,
                   const struct ble_gatt_chr *chr,
                   void *arg)
{
    if (error->status == 0 && chr != NULL)
    {
        uint16_t uuid16 = ble_uuid_u16(&chr->uuid.u);

        switch (uuid16)
        {
        case 0x2A00: // Device Name
            g_conn_state.gap_device_name_chr_val_handle = chr->val_handle;
            ESP_LOGI(tag, "Device Name (0x2A00) discovered (handle=0x%04X)", chr->val_handle);
            break;
        case 0x2A01: // Appearance
            g_conn_state.gap_appearance_chr_val_handle = chr->val_handle;
            ESP_LOGI(tag, "Appearance (0x2A01) discovered (handle=0x%04X)", chr->val_handle);
            break;
        case 0x2A04: // Peripheral Preferred Connection Parameters
            g_conn_state.gap_ppcp_chr_val_handle = chr->val_handle;
            ESP_LOGI(tag, "PPCP (0x2A04) discovered (handle=0x%04X)", chr->val_handle);
            break;
        }
        return 0;
    }

    if (error->status == BLE_HS_EDONE)
    {
        blecent_start_battery_service_disc(conn_handle);
        return 0;
    }

    ESP_LOGE(tag, "GAP characteristic discovery failed; status=%d", error->status);
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return error->status;
}

static int
blecent_on_device_name_read(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr,
                            void *arg)
{
    if (error->status == 0)
    {
        if (attr->om != NULL && OS_MBUF_PKTLEN(attr->om) > 0)
        {
            uint16_t len = OS_MBUF_PKTLEN(attr->om);
            char *name = (char *)malloc(len + 1);
            if (name != NULL)
            {
                os_mbuf_copydata(attr->om, 0, len, name);
                name[len] = '\0';
                ESP_LOGI(tag, "Device Name: %s", name);
                free(name);
            }
        }
        else
        {
            ESP_LOGW(tag, "Device Name read: empty or invalid data");
        }
    }
    else
    {
        ESP_LOGE(tag, "Device Name read failed; status=%d", error->status);
        return error->status;
    }
    return 0;
}

static int
blecent_on_device_name_write(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             struct ble_gatt_attr *attr,
                             void *arg)
{
    if (error->status == 0)
    {
        ESP_LOGI(tag, "Device Name write successful (handle=0x%04X)", attr->handle);
    }
    else
    {
        ESP_LOGE(tag, "Device Name write failed; status=%d", error->status);
        return error->status;
    }
    return 0;
}

static int
blecent_on_appearance_read(uint16_t conn_handle,
                           const struct ble_gatt_error *error,
                           struct ble_gatt_attr *attr,
                           void *arg)
{
    if (error->status == 0)
    {
        if (attr->om != NULL && OS_MBUF_PKTLEN(attr->om) >= 2)
        {
            uint16_t appearance = 0;
            os_mbuf_copydata(attr->om, 0, 2, &appearance);
            ESP_LOGI(tag, "Appearance: 0x%04X (%d) - %s", appearance, appearance,
                     appearance == 0x03C1 ? "Keyboard (HID subtype)" : "Unknown");
        }
        else
        {
            ESP_LOGW(tag, "Appearance read: empty or invalid data");
        }
    }
    else
    {
        ESP_LOGE(tag, "Appearance read failed; status=%d", error->status);
        return error->status;
    }
    return 0;
}

static int
blecent_on_ppcp_read(uint16_t conn_handle,
                     const struct ble_gatt_error *error,
                     struct ble_gatt_attr *attr,
                     void *arg)
{
    if (error->status == 0)
    {
        if (attr->om != NULL && OS_MBUF_PKTLEN(attr->om) >= 8)
        {
            uint16_t min_interval, max_interval, slave_latency, timeout_mult;
            os_mbuf_copydata(attr->om, 0, 2, &min_interval);
            os_mbuf_copydata(attr->om, 2, 2, &max_interval);
            os_mbuf_copydata(attr->om, 4, 2, &slave_latency);
            os_mbuf_copydata(attr->om, 6, 2, &timeout_mult);

            float min_ms = min_interval * 1.25f;
            float max_ms = max_interval * 1.25f;
            float timeout_ms = timeout_mult * 10.0f;

            ESP_LOGI(tag, "PPCP: Connection Interval: %.2fms - %.2fms, "
                          "Slave Latency: %d, Supervision Timeout: %.2fms",
                     min_ms, max_ms, slave_latency, timeout_ms);
        }
        else
        {
            ESP_LOGW(tag, "PPCP read: empty or invalid data");
        }
    }
    else
    {
        ESP_LOGE(tag, "PPCP read failed; status=%d", error->status);
        return error->status;
    }
    return 0;
}

static int blecent_read_device_name(uint16_t conn_handle)
{
    if (g_conn_state.gap_device_name_chr_val_handle != 0)
    {
        int rc = ble_gattc_read(conn_handle, g_conn_state.gap_device_name_chr_val_handle,
                                blecent_on_device_name_read, NULL);
        if (rc != 0)
        {
            ESP_LOGE(tag, "Failed to read device name; rc=%d", rc);
            return rc;
        }
        return 0;
    }
    else
    {
        ESP_LOGW(tag, "Device Name characteristic not found");
        return BLE_HS_ENOENT;
    }
}

static int blecent_write_device_name(uint16_t conn_handle, const char *name)
{
    if (g_conn_state.gap_device_name_chr_val_handle != 0)
    {
        size_t len = strlen(name);
        if (len > 20) /* GATT characteristic value max length is typically 20 bytes */
        {
            ESP_LOGE(tag, "Device name too long (max 20 bytes)");
            return BLE_HS_EINVAL;
        }

        int rc = ble_gattc_write_flat(conn_handle, g_conn_state.gap_device_name_chr_val_handle,
                                      name, len,
                                      blecent_on_device_name_write, NULL);
        if (rc != 0)
        {
            ESP_LOGE(tag, "Failed to write device name; rc=%d", rc);
            return rc;
        }
        ESP_LOGI(tag, "Writing device name: %s", name);
        return 0;
    }
    else
    {
        ESP_LOGW(tag, "Device Name characteristic not found");
        return BLE_HS_ENOENT;
    }
}

static int blecent_read_appearance(uint16_t conn_handle)
{
    if (g_conn_state.gap_appearance_chr_val_handle != 0)
    {
        int rc = ble_gattc_read(conn_handle, g_conn_state.gap_appearance_chr_val_handle,
                                blecent_on_appearance_read, NULL);
        if (rc != 0)
        {
            ESP_LOGE(tag, "Failed to read appearance; rc=%d", rc);
            return rc;
        }
        return 0;
    }
    else
    {
        ESP_LOGW(tag, "Appearance characteristic not found");
        return BLE_HS_ENOENT;
    }
}

static int blecent_read_ppcp(uint16_t conn_handle)
{
    if (g_conn_state.gap_ppcp_chr_val_handle != 0)
    {
        int rc = ble_gattc_read(conn_handle, g_conn_state.gap_ppcp_chr_val_handle,
                                blecent_on_ppcp_read, NULL);
        if (rc != 0)
        {
            ESP_LOGE(tag, "Failed to read PPCP; rc=%d", rc);
            return rc;
        }
        return 0;
    }
    else
    {
        ESP_LOGW(tag, "PPCP characteristic not found");
        return BLE_HS_ENOENT;
    }
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
        blecent_start_hid_service_disc(conn_handle);
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
        blecent_start_hid_service_disc(conn_handle);
        return 0;
    }

    ESP_LOGE(tag, "Battery Level characteristic discovery failed; status=%d",
             error->status);
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return error->status;
}

static void
blecent_start_hid_service_disc(uint16_t conn_handle)
{
    int rc = ble_gattc_disc_svc_by_uuid(conn_handle, &hid_svc_uuid.u,
                                        blecent_on_hid_svc, NULL);
    if (rc != 0)
    {
        ESP_LOGE(tag, "HID service discovery start failed; rc=%d", rc);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static int
blecent_on_hid_svc(uint16_t conn_handle,
                   const struct ble_gatt_error *error,
                   const struct ble_gatt_svc *service,
                   void *arg)
{
    if (error->status == 0)
    {
        g_conn_state.hid_start_handle = service->start_handle;
        g_conn_state.hid_end_handle = service->end_handle;
        ESP_LOGI(tag, "HID service discovered (start=0x%04X end=0x%04X)",
                 service->start_handle, service->end_handle);
        return 0;
    }

    if (error->status == BLE_HS_EDONE)
    {
        blecent_start_hid_chr_disc(conn_handle);
        return 0;
    }

    ESP_LOGE(tag, "HID service discovery failed; status=%d",
             error->status);
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return error->status;
}

static void
blecent_start_hid_chr_disc(uint16_t conn_handle)
{
    int rc;

    if (g_conn_state.hid_start_handle == 0)
    {
        ESP_LOGW(tag, "HID service not found on peer");
        ESP_LOGI(tag, "Discovery complete for connection 0x%04X", g_conn_state.conn_handle);
        return;
    }

    rc = ble_gattc_disc_all_chrs(conn_handle,
                                 g_conn_state.hid_start_handle,
                                 g_conn_state.hid_end_handle,
                                 blecent_on_hid_chr, NULL);
    if (rc != 0)
    {
        ESP_LOGE(tag, "HID characteristic discovery start failed; rc=%d", rc);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static int blecent_on_hid_dsc(uint16_t conn_handle,
                              const struct ble_gatt_error *error,
                              uint16_t chr_val_handle,
                              const struct ble_gatt_dsc *dsc,
                              void *arg);

static void blecent_start_hid_dsc_disc(uint16_t conn_handle, uint16_t chr_val_handle, uint16_t end_handle);

static int
blecent_on_hid_chr(uint16_t conn_handle,
                   const struct ble_gatt_error *error,
                   const struct ble_gatt_chr *chr,
                   void *arg)
{
    if (error->status == 0 && chr != NULL)
    {
        uint16_t uuid16 = ble_uuid_u16(&chr->uuid.u);

        switch (uuid16)
        {
        case 0x2A4A: // HID Information
            g_conn_state.hid_info_chr_val_handle = chr->val_handle;
            ESP_LOGI(tag, "HID Information (0x2A4A) discovered (handle=0x%04X)", chr->val_handle);
            break;
        case 0x2A22: // Boot Keyboard Input Report
            g_conn_state.hid_boot_kb_input_chr_val_handle = chr->val_handle;
            ESP_LOGI(tag, "Boot Keyboard Input Report (0x2A22) discovered (handle=0x%04X)", chr->val_handle);
            blecent_start_hid_dsc_disc(conn_handle, chr->val_handle, g_conn_state.hid_end_handle);
            break;
        case 0x2A32: // Boot Keyboard Output Report
            g_conn_state.hid_boot_kb_output_chr_val_handle = chr->val_handle;
            ESP_LOGI(tag, "Boot Keyboard Output Report (0x2A32) discovered (handle=0x%04X)", chr->val_handle);
            break;
        case 0x2A4B: // Report Map
            g_conn_state.hid_report_map_chr_val_handle = chr->val_handle;
            ESP_LOGI(tag, "Report Map (0x2A4B) discovered (handle=0x%04X)", chr->val_handle);
            break;
        case 0x2A4D: // Report
            g_conn_state.hid_report_chr_val_handles[g_conn_state.hid_report_count] = chr->val_handle;
            g_conn_state.hid_report_cccd_handles[g_conn_state.hid_report_count] = 0;
            ESP_LOGI(tag, "Report (0x2A4D) #%d discovered (handle=0x%04X)", g_conn_state.hid_report_count + 1, chr->val_handle);
            blecent_start_hid_dsc_disc(conn_handle, chr->val_handle, g_conn_state.hid_end_handle);
            g_conn_state.hid_report_count++;
            break;
        case 0x2A4C: // HID Control Point
            g_conn_state.hid_control_point_chr_val_handle = chr->val_handle;
            ESP_LOGI(tag, "HID Control Point (0x2A4C) discovered (handle=0x%04X)", chr->val_handle);
            break;
        case 0x2A4E: // Protocol Mode
            g_conn_state.hid_protocol_mode_chr_val_handle = chr->val_handle;
            ESP_LOGI(tag, "Protocol Mode (0x2A4E) discovered (handle=0x%04X)", chr->val_handle);
            break;
        }
        return 0;
    }

    if (error->status == BLE_HS_EDONE)
    {
        ESP_LOGI(tag, "HID characteristic discovery complete");
        ESP_LOGI(tag, "Discovery complete for connection 0x%04X", g_conn_state.conn_handle);
        return 0;
    }

    ESP_LOGE(tag, "HID characteristic discovery failed; status=%d", error->status);
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return error->status;
}

static void
blecent_start_hid_dsc_disc(uint16_t conn_handle, uint16_t chr_val_handle, uint16_t end_handle)
{
    int rc = ble_gattc_disc_all_dscs(conn_handle, chr_val_handle + 1, end_handle, blecent_on_hid_dsc, (void *)(uintptr_t)chr_val_handle);
    if (rc != 0)
    {
        ESP_LOGW(tag, "Failed to start descriptor discovery for characteristic 0x%04X; rc=%d", chr_val_handle, rc);
    }
}

static int
blecent_on_hid_dsc(uint16_t conn_handle,
                   const struct ble_gatt_error *error,
                   uint16_t chr_val_handle,
                   const struct ble_gatt_dsc *dsc,
                   void *arg)
{
    uint16_t target_chr_handle = (uint16_t)(uintptr_t)arg;

    if (error->status == 0 && dsc != NULL)
    {
        uint16_t uuid16 = ble_uuid_u16(&dsc->uuid.u);

        if (uuid16 == 0x2902) // CCCD
        {
            for (uint8_t i = 0; i < g_conn_state.hid_report_count; i++) // czy to report char?
            {
                if (target_chr_handle == g_conn_state.hid_report_chr_val_handles[i])
                {
                    g_conn_state.hid_report_cccd_handles[i] = dsc->handle;
                    ESP_LOGI(tag, "Report #%d CCCD discovered (chr=0x%04X, cccd=0x%04X)", i + 1, target_chr_handle, dsc->handle);
                    return 0;
                }
            }

            if (target_chr_handle == g_conn_state.hid_boot_kb_input_chr_val_handle) // czy to boot keyboard input?
            {
                g_conn_state.hid_boot_kb_input_cccd_handle = dsc->handle;
                ESP_LOGI(tag, "Boot Keyboard Input CCCD discovered (handle=0x%04X)", dsc->handle);
            }
        }
        return 0;
    }

    if (error->status == BLE_HS_EDONE)
    {
        ESP_LOGI(tag, "Descriptor discovery complete for characteristic 0x%04X", target_chr_handle);
        return 0;
    }
    return 0;
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
            ESP_LOGI(tag, "\nBattery Level read: %d%%", battery_level);
        }
        else
        {
            ESP_LOGW(tag, "\nBattery Level read: empty or invalid data");
        }
    }
    else
    {
        ESP_LOGE(tag, "\nBattery Level read failed; status=%d", error->status);
        return error->status;
    }

    return 0;
}

static int blecent_read_battery(uint16_t conn_handle)
{
    if (g_conn_state.battery_chr_val_handle != 0)
    {
        int rc = ble_gattc_read(conn_handle, g_conn_state.battery_chr_val_handle, blecent_on_battery_read, NULL);
        if (rc != 0)
        {
            ESP_LOGE(tag, "\nFailed to read battery level; rc=%d", rc);
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

static int blecent_on_protocol_mode_read(uint16_t conn_handle,
                                         const struct ble_gatt_error *error,
                                         struct ble_gatt_attr *attr,
                                         void *arg)
{
    if (error->status == 0)
    {
        uint8_t mode = 0;
        if (attr->om != NULL && OS_MBUF_PKTLEN(attr->om) >= 1)
        {
            os_mbuf_copydata(attr->om, 0, 1, &mode);

            if (mode > 1)
            {
                ESP_LOGW(tag, "Protocol Mode read: invalid value 0x%02X (%d), clamping to Report Protocol (1)", mode, mode);
                mode = 1;
            }

            g_conn_state.current_protocol_mode = mode;
            ESP_LOGI(tag, "Protocol Mode read: %s (0x%02X) (handle=0x%04X)",
                     mode == 0 ? "Boot Protocol" : "Report Protocol", mode, attr->handle);
        }
        else
        {
            ESP_LOGW(tag, "Protocol Mode read: empty or invalid data");
        }
    }
    else
    {
        ESP_LOGE(tag, "Protocol Mode read failed; status=%d", error->status);
        return error->status;
    }
    return 0;
}

static int blecent_read_protocol_mode(uint16_t conn_handle)
{
    if (g_conn_state.hid_protocol_mode_chr_val_handle != 0)
    {
        int rc = ble_gattc_read(conn_handle, g_conn_state.hid_protocol_mode_chr_val_handle,
                                blecent_on_protocol_mode_read, NULL);
        if (rc != 0)
        {
            ESP_LOGE(tag, "Failed to read protocol mode; rc=%d", rc);
            return rc;
        }
        return 0;
    }
    else
    {
        ESP_LOGW(tag, "Protocol Mode characteristic not found");
        return BLE_HS_ENOENT;
    }
}

static int blecent_on_keyboard_subscribe(uint16_t conn_handle,
                                         const struct ble_gatt_error *error,
                                         struct ble_gatt_attr *attr,
                                         void *arg)
{
    if (error->status == 0)
    {
        ESP_LOGD(tag, "CCCD write successful (handle=0x%04X)", attr->handle);
    }
    return 0;
}

static int blecent_subscribe_keyboard(uint16_t conn_handle, uint8_t enable)
{
    uint8_t cccd_value[2] = {enable ? 0x01 : 0x00, 0x00};
    int total_subscribed = 0;
    int total_failed = 0;

    ESP_LOGI(tag, "Subscribing to keyboard notifications (current protocol mode: %s, mode value: %d)",
             g_conn_state.current_protocol_mode == 0 ? "Boot Protocol" : "Report Protocol",
             g_conn_state.current_protocol_mode);

    if (g_conn_state.current_protocol_mode == 0)
    {
        if (g_conn_state.hid_boot_kb_input_chr_val_handle != 0)
        {
            uint16_t cccd_handle = g_conn_state.hid_boot_kb_input_cccd_handle;
            if (cccd_handle == 0)
            {
                cccd_handle = g_conn_state.hid_boot_kb_input_chr_val_handle + 1;
                ESP_LOGW(tag, "Boot Keyboard Input CCCD not discovered, trying standard location (chr+1=0x%04X)", cccd_handle);
            }

            ESP_LOGI(tag, "%s Boot Keyboard Input Report (0x2A22, chr=0x%04X, cccd=0x%04X) for keyboard notifications",
                     enable ? "Subscribing to" : "Unsubscribing from",
                     g_conn_state.hid_boot_kb_input_chr_val_handle,
                     cccd_handle);

            int rc = ble_gattc_write_flat(conn_handle, cccd_handle, cccd_value, sizeof(cccd_value), blecent_on_keyboard_subscribe, NULL);
            if (rc == 0)
            {
                total_subscribed++;
                if (g_conn_state.hid_boot_kb_input_cccd_handle == 0)
                {
                    g_conn_state.hid_boot_kb_input_cccd_handle = cccd_handle;
                    ESP_LOGI(tag, "Boot Keyboard Input CCCD confirmed at handle 0x%04X", cccd_handle);
                }
            }
            else
            {
                ESP_LOGE(tag, "Failed to %s Boot Keyboard Input notifications; rc=%d",
                         enable ? "subscribe to" : "unsubscribe from", rc);
                total_failed++;
            }
        }
        else
        {
            ESP_LOGE(tag, "Boot Keyboard Input characteristic not found");
            total_failed++;
        }
    }
    else
    {
        for (uint8_t i = 0; i < g_conn_state.hid_report_count; i++)
        {
            if (g_conn_state.hid_report_chr_val_handles[i] != 0)
            {
                uint16_t cccd_handle = g_conn_state.hid_report_cccd_handles[i];

                if (cccd_handle == 0)
                {
                    cccd_handle = g_conn_state.hid_report_chr_val_handles[i] + 1;
                    ESP_LOGW(tag, "Report #%d CCCD not discovered, trying standard location (chr+1=0x%04X)", i + 1, cccd_handle);
                }

                ESP_LOGI(tag, "%s Report #%d characteristic (0x2A4D, chr=0x%04X, cccd=0x%04X) for keyboard notifications",
                         enable ? "Subscribing to" : "Unsubscribing from", i + 1,
                         g_conn_state.hid_report_chr_val_handles[i],
                         cccd_handle);

                int rc = ble_gattc_write_flat(conn_handle, cccd_handle,
                                              cccd_value, sizeof(cccd_value),
                                              blecent_on_keyboard_subscribe, NULL);
                if (rc == 0)
                {
                    total_subscribed++;
                    if (g_conn_state.hid_report_cccd_handles[i] == 0)
                    {
                        g_conn_state.hid_report_cccd_handles[i] = cccd_handle;
                        ESP_LOGI(tag, "Report #%d CCCD confirmed at handle 0x%04X", i + 1, cccd_handle);
                    }
                }
            }
        }
    }

    if (total_subscribed == 0 && total_failed == 0)
    {
        ESP_LOGE(tag, "No keyboard input characteristics with CCCD found");
        ESP_LOGE(tag, "Found %d Report characteristics, Boot chr=0x%04X cccd=0x%04X",
                 g_conn_state.hid_report_count,
                 g_conn_state.hid_boot_kb_input_chr_val_handle,
                 g_conn_state.hid_boot_kb_input_cccd_handle);
        return BLE_HS_ENOENT;
    }

    ESP_LOGI(tag, "Subscription complete: %d succeeded, %d failed", total_subscribed, total_failed);

    return (total_subscribed > 0) ? 0 : -1;
}

static int blecent_on_protocol_mode_write(uint16_t conn_handle,
                                          const struct ble_gatt_error *error,
                                          struct ble_gatt_attr *attr,
                                          void *arg)
{
    if (error->status == 0)
    {
        blecent_subscribe_keyboard(conn_handle, 0);
        ESP_LOGI(tag, "Protocol Mode write successful (handle=0x%04X, mode=%d)", attr->handle, g_conn_state.current_protocol_mode);
    }
    else
    {
        ESP_LOGE(tag, "Protocol Mode write failed; status=%d", error->status);
        return error->status;
    }
    return 0;
}

static int blecent_write_protocol_mode(uint16_t conn_handle, uint8_t mode)
{
    if (g_conn_state.hid_protocol_mode_chr_val_handle != 0)
    {
        if (mode > 1)
        {
            ESP_LOGE(tag, "Invalid protocol mode: %d (must be 0 or 1)", mode);
            return BLE_HS_EINVAL;
        }
        g_conn_state.current_protocol_mode = mode;

        int rc = ble_gattc_write_flat(conn_handle, g_conn_state.hid_protocol_mode_chr_val_handle,
                                      &mode, sizeof(mode),
                                      blecent_on_protocol_mode_write, NULL);
        if (rc != 0)
        {
            ESP_LOGE(tag, "Failed to write protocol mode; rc=%d", rc);
            return rc;
        }
        ESP_LOGI(tag, "Setting Protocol Mode to %s (0x%02X)",
                 mode == 0 ? "Boot Protocol" : "Report Protocol", mode);
        return 0;
    }
    else
    {
        ESP_LOGW(tag, "Protocol Mode characteristic not found");
        return BLE_HS_ENOENT;
    }
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

    rc = ble_gap_connect(own_addr_type, addr, 30000, NULL, blecent_gap_event, NULL);
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

            int rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc != 0)
            {
                ESP_LOGW(tag, "Failed to initiate security; rc=%d (starting discovery anyway)", rc);
                blecent_start_gap_service_disc(event->connect.conn_handle);
            }
            else
            {
                ESP_LOGI(tag, "Security/pairing initiated - waiting for encryption");
            }
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

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(tag, "Conn update event; status=%d", event->conn_update.status);
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(tag, "discovery complete; reason=%d",
                 event->disc_complete.reason);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        uint16_t notify_handle = event->notify_rx.attr_handle;
        uint16_t notify_len = OS_MBUF_PKTLEN(event->notify_rx.om);

        uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
        if (event->notify_rx.om != NULL && len > 0)
        {
            uint8_t *data = (uint8_t *)malloc(len);
            if (data != NULL)
            {
                int rc = os_mbuf_copydata(event->notify_rx.om, 0, len, data);
                if (rc == 0)
                {
                    keyboard_simulator_process_report(data, len);
                }
                else
                {
                    ESP_LOGE(tag, "Failed to copy notification data; rc=%d", rc);
                }
                free(data);
            }
            else
            {
                ESP_LOGE(tag, "Failed to allocate memory for notification data (len=%d)", len);
            }
        }

    case BLE_GAP_EVENT_ENC_CHANGE:
    {
        struct ble_gap_conn_desc desc;
        int rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        if (rc == 0)
        {
            ESP_LOGI(tag, "Encryption change event; status=%d conn_handle=%d encrypted=%d",
                     event->enc_change.status,
                     event->enc_change.conn_handle,
                     desc.sec_state.encrypted);

            if (event->enc_change.status == 0 && desc.sec_state.encrypted)
            {
                ESP_LOGI(tag, "Connection encrypted");
                if (g_conn_state.gap_start_handle == 0 &&
                    g_conn_state.conn_handle == event->enc_change.conn_handle)
                {
                    blecent_start_gap_service_disc(event->enc_change.conn_handle);
                }
            }
        }
        return 0;
    }

    case BLE_GAP_EVENT_LINK_ESTAB:
        ESP_LOGI(tag, "Link established with status: %d", event->link_estab.status);
        return 0;

    case BLE_GAP_EVENT_CTE_REQ_FAILED:
        ESP_LOGI(tag, "CTE request failed (direction finding feature not available)");
        return 0;

    default:
        ESP_LOGI(tag, "Unhandled event: %x", event->type);
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
    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    blecent_scan();
}

void blecent_host_task(void *param)
{
    ESP_LOGI(tag, "BLE Host Task Started");
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

int gatt_client_subscribe_keyboard(uint8_t enable)
{
    if (g_conn_state.conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGE(tag, "Not connected to any device");
        return BLE_HS_ENOTCONN;
    }

    keyboard_simulator_set_enabled(enable != 0);
    return blecent_subscribe_keyboard(g_conn_state.conn_handle, enable);
}

int gatt_client_read_protocol_mode(void)
{
    if (g_conn_state.conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGE(tag, "Not connected to any device");
        return BLE_HS_ENOTCONN;
    }
    return blecent_read_protocol_mode(g_conn_state.conn_handle);
}

int gatt_client_write_protocol_mode(uint8_t mode)
{
    if (g_conn_state.conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGE(tag, "Not connected to any device");
        return BLE_HS_ENOTCONN;
    }
    return blecent_write_protocol_mode(g_conn_state.conn_handle, mode);
}

int gatt_client_read_device_name(void)
{
    if (g_conn_state.conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGE(tag, "Not connected to any device");
        return BLE_HS_ENOTCONN;
    }
    return blecent_read_device_name(g_conn_state.conn_handle);
}

int gatt_client_write_device_name(const char *name)
{
    if (g_conn_state.conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGE(tag, "Not connected to any device");
        return BLE_HS_ENOTCONN;
    }
    return blecent_write_device_name(g_conn_state.conn_handle, name);
}

int gatt_client_read_appearance(void)
{
    if (g_conn_state.conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGE(tag, "Not connected to any device");
        return BLE_HS_ENOTCONN;
    }
    return blecent_read_appearance(g_conn_state.conn_handle);
}

int gatt_client_read_ppcp(void)
{
    if (g_conn_state.conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        ESP_LOGE(tag, "Not connected to any device");
        return BLE_HS_ENOTCONN;
    }
    return blecent_read_ppcp(g_conn_state.conn_handle);
}

void start_gatt_client(void)
{
    int rc;
    blecent_reset_conn_state();
    keyboard_simulator_init();

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