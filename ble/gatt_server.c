#include "common.h"
#include "gap.h"
#include "gatt_svc.h"
#include "host/ble_gap.h"
#include "event_manager.h"
#include "freertos/timers.h"

#define GATT_SERVER_TIMEOUT_MS (10 * 60 * 1000)

static const char *TAG = "gatt_server";

void ble_store_config_init(void);

static void on_stack_reset(int reason);
static void on_stack_sync(void);
static void nimble_host_config_init(void);
static void nimble_host_task(void *param);
static void start_gatt_server();
static void stop_gatt_server();

static void on_stack_reset(int reason)
{
    ESP_LOGI(TAG, "nimble stack reset, reset reason: %d", reason);
}

static void on_stack_sync(void)
{
    adv_init();
}

static void nimble_host_config_init(void)
{
    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_store_config_init();
}

static TaskHandle_t nimble_host_task_handle = NULL;
static TimerHandle_t gatt_timeout_timer = NULL;
static bool gatt_server_active = false;

static void gatt_timeout_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "GATT server timeout reached, stopping...");
    stop_gatt_server();
    gatt_server_active = false;
    event_manager_clear_bits(EVENT_BIT_CONFIG_MODE);

    if (gatt_timeout_timer != NULL)
    {
        xTimerDelete(gatt_timeout_timer, 0);
        gatt_timeout_timer = NULL;
    }
}

static void gatt_server_manager_task(void *param)
{
    uint32_t notification = 0;
    TaskHandle_t task_handle = xTaskGetCurrentTaskHandle();

    event_manager_register_notification(task_handle, EVENT_BIT_BUTTON_PRESSED | EVENT_BIT_WIFI_CONFIG_SAVED);

    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(
            EVENT_BIT_BUTTON_PRESSED | EVENT_BIT_WIFI_CONFIG_SAVED,
            true,  // Clear on exit
            false, // Wait for any
            portMAX_DELAY);

        if (bits & EVENT_BIT_WIFI_CONFIG_SAVED)
        {
            if (gatt_server_active)
            {
                ESP_LOGI(TAG, "WiFi config saved, stopping GATT server...");
                stop_gatt_server();
                gatt_server_active = false;
                event_manager_clear_bits(EVENT_BIT_CONFIG_MODE);

                if (gatt_timeout_timer != NULL)
                {
                    xTimerStop(gatt_timeout_timer, 0);
                    xTimerDelete(gatt_timeout_timer, 0);
                    gatt_timeout_timer = NULL;
                }
            }
        }
        else if (bits & EVENT_BIT_BUTTON_PRESSED)
        {
            if (!gatt_server_active)
            {
                ESP_LOGI(TAG, "Starting GATT server...");
                start_gatt_server();
                gatt_server_active = true;
                event_manager_set_bits(EVENT_BIT_CONFIG_MODE);

                if (gatt_timeout_timer == NULL)
                {
                    gatt_timeout_timer = xTimerCreate(
                        "gatt_timeout",
                        pdMS_TO_TICKS(GATT_SERVER_TIMEOUT_MS),
                        pdFALSE, // One-shot timer
                        NULL,
                        gatt_timeout_callback);

                    if (gatt_timeout_timer != NULL)
                    {
                        xTimerStart(gatt_timeout_timer, 0);
                        ESP_LOGI(TAG, "GATT server will auto-stop in 10 minutes");
                    }
                    else
                    {
                        ESP_LOGE(TAG, "Failed to create timeout timer");
                    }
                }
            }
            else
            {
                ESP_LOGI(TAG, "Stopping GATT server...");
                stop_gatt_server();
                gatt_server_active = false;
                event_manager_clear_bits(EVENT_BIT_CONFIG_MODE);

                if (gatt_timeout_timer != NULL)
                {
                    xTimerStop(gatt_timeout_timer, 0);
                    xTimerDelete(gatt_timeout_timer, 0);
                    gatt_timeout_timer = NULL;
                }
            }
        }
    }
}

static void nimble_host_task(void *param)
{
    ESP_LOGI(TAG, "nimble host task has been started!");
    nimble_port_run();
    nimble_host_task_handle = NULL;
    vTaskDelete(NULL);
}

static void start_gatt_server(void)
{
    int rc;
    esp_err_t ret;

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to initialize nvs flash, error code: %d ", ret);
        return;
    }

    ret = nimble_port_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "failed to initialize nimble stack, error code: %d ",
                 ret);
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
    xTaskCreate(nimble_host_task, "NimBLE Host", 4 * 1024, NULL, 5, &nimble_host_task_handle);
    ESP_LOGI(TAG, "GATT server started");
    return;
}

static void stop_gatt_server(void)
{
    int rc;

    rc = ble_gap_adv_stop();
    if (rc != 0 && rc != BLE_HS_EALREADY)
    {
        ESP_LOGW(TAG, "Failed to stop advertising, error code: %d", rc);
    }

    nimble_port_stop();

    if (nimble_host_task_handle != NULL)
    {
        vTaskDelay(pdMS_TO_TICKS(100)); // Give task time to clean up
        if (nimble_host_task_handle != NULL)
        {
            vTaskDelete(nimble_host_task_handle);
            nimble_host_task_handle = NULL;
        }
    }

    nimble_port_deinit();
    gatt_server_active = false;
    ESP_LOGI(TAG, "GATT server stopped");
}

void gatt_server_manager_init(void)
{
    xTaskCreate(
        gatt_server_manager_task,
        "gatt_server_manager",
        4096,
        NULL,
        5,
        NULL);

    ESP_LOGI(TAG, "GATT server manager initialized");
}
