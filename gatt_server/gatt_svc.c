#include "gatt_svc.h"
#include "common.h"
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"
#include "battery_service.h"
#include "wifi_config_service.h"
#include "esp_log.h"

static const char *TAG = "NimBLE_GATT_Server";

/* Build combined service definition array */
static struct ble_gatt_svc_def gatt_svr_svcs[16]; /* Adjust size as needed */
static int gatt_svr_svcs_count = 0;

static void build_combined_svc_def(void)
{
    int i, j;
    const struct ble_gatt_svc_def *all_svc_defs[] = {
        battery_service_get_svc_def(),
        wifi_config_service_get_svc_def(),
        NULL};

    gatt_svr_svcs_count = 0;

    for (i = 0; all_svc_defs[i] != NULL; i++)
    {
        const struct ble_gatt_svc_def *svc_def = all_svc_defs[i];
        for (j = 0; svc_def[j].type != 0; j++)
        {
            if (gatt_svr_svcs_count < (int)(sizeof(gatt_svr_svcs) / sizeof(gatt_svr_svcs[0]) - 1))
            {
                gatt_svr_svcs[gatt_svr_svcs_count] = svc_def[j];
                gatt_svr_svcs_count++;
            }
        }
    }

    /* Terminate array */
    gatt_svr_svcs[gatt_svr_svcs_count].type = 0;
}

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    battery_service_register_cb(ctxt, arg);
    wifi_config_service_register_cb(ctxt, arg);
}

void gatt_svr_subscribe_cb(struct ble_gap_event *event)
{
    battery_service_subscribe_cb(event);
    wifi_config_service_subscribe_cb(event);
}

int gatt_svc_init(void)
{
    int rc;

    rc = battery_service_init();
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to initialize battery service: %d", rc);
        return rc;
    }

    rc = wifi_config_service_init();
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to initialize WiFi config service: %d", rc);
        return rc;
    }

    /* 1. GATT service initialization */
    ble_svc_gatt_init();

    /* 2. Build combined service definition */
    build_combined_svc_def();

    /* 3. Update GATT services counter */
    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to count GATT services: %d", rc);
        return rc;
    }

    /* 4. Add GATT services */
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to add GATT services: %d", rc);
        return rc;
    }

    ESP_LOGI(TAG, "GATT server initialized with %d services", gatt_svr_svcs_count);
    return 0;
}