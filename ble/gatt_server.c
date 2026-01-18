#include "common.h"
#include "gap.h"
#include "gatt_svc.h"
#include "host/ble_gap.h"
#include "host/ble_store.h"
#include "host/ble_gatt.h"
#include "store/config/ble_store_config.h"
#include "event_manager.h"
#include "freertos/timers.h"

#define GATT_SERVER_TIMEOUT_MS (10 * 60 * 1000)

static const char *TAG = "gatt_server";

static TaskHandle_t nimble_host_task_handle = NULL;

static void on_stack_reset(int reason);
static void on_stack_sync(void);
static void nimble_host_config_init(void);
static void nimble_host_task(void *param);
void gatt_server_init(void);
void ble_store_config_init(void);

static void on_stack_reset(int reason)
{
    ESP_LOGI(TAG, "nimble stack reset, reset reason: %d", reason);
}

static void on_stack_sync(void)
{
    ESP_LOGI(TAG, "NimBLE stack synchronized");
}

static void nimble_host_config_init(void)
{
    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.gatts_register_arg = NULL;

    ble_store_config_init();
}

static void nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_host_task_handle = NULL;
    vTaskDelete(NULL);
}

void gatt_server_init(void)
{
    int rc;
    esp_err_t ret;

    ret = nimble_port_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to initialize nimble stack, error code: %d ", ret);
        return;
    }

    rc = gap_init();
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to initialize GAP service, error code: %d", rc);
        return;
    }

    rc = gatt_svc_init();
    if (rc != 0)
    {
        ESP_LOGE(TAG, "failed to initialize GATT server, error code: %d", rc);
        return;
    }

    nimble_host_config_init();
    xTaskCreate(nimble_host_task, "NimBLE Host", 8 * 1024, NULL, 5, &nimble_host_task_handle);
    ESP_LOGI(TAG, "NimBLE stack initialized");
}

void start_gatt_server(void)
{

    adv_init();
    ESP_LOGI(TAG, "GATT server advertising started");
}

void stop_gatt_server(void)
{
    int rc;

    rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY)
    {
        ESP_LOGW(TAG, "Failed to stop advertising, error code: %d", rc);
    }
    else
    {
        ESP_LOGI(TAG, "GATT server advertising stopped");
    }
}