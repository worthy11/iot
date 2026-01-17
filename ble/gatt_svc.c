#include "gatt_svc.h"
#include "common.h"
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"
#include "provisioning_service.h"
#include "telemetry_service.h"
#include "command_service.h"

static const char *TAG = "GATT_SVC";

static struct ble_gatt_svc_def gatt_svr_svcs[16];
static int gatt_svr_svcs_count = 0;

static void build_combined_svc_def(void)
{
    int i, j;
    const struct ble_gatt_svc_def *all_svc_defs[] = {
        provisioning_service_get_svc_def(),
        telemetry_service_get_svc_def(),
        command_service_get_svc_def(),
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

    gatt_svr_svcs[gatt_svr_svcs_count].type = 0;
}

int gatt_svc_init(void)
{
    int rc;

    ble_svc_gatt_init();
    build_combined_svc_def();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to count GATT services: %d", rc);
        return rc;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to add GATT services: %d", rc);
        return rc;
    }

    ESP_LOGI(TAG, "GATT server initialized with %d services", gatt_svr_svcs_count);
    return 0;
}