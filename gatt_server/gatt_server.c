#include "common.h"
#include "gap.h"
#include "gatt_svc.h"
#include "battery_service.h"
#include "led_service.h"
#include "keyboard_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include "esp_vfs_dev.h"
#include "driver/uart.h"

#define KEYBOARD_TASK_PERIOD pdMS_TO_TICKS(10000)
#define BATTERY_LEVEL_TASK_PERIOD pdMS_TO_TICKS(1000)

static const char *TAG = "server_main";

void ble_store_config_init(void);

static void on_stack_reset(int reason);
static void on_stack_sync(void);
static void nimble_host_config_init(void);
static void nimble_host_task(void *param);


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

static void nimble_host_task(void *param)
{
    ESP_LOGI(TAG, "nimble host task has been started!");

    nimble_port_run();

    vTaskDelete(NULL);
}

static void battery_level_task(void *param)
{
    ESP_LOGI(TAG, "battery level task has been started!");

    while (1)
    {
        update_battery_level();
        send_battery_level_indication();
        vTaskDelay(BATTERY_LEVEL_TASK_PERIOD);
    }

    vTaskDelete(NULL);
}


void gatt_server_main(void)
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

    led_init();
    nimble_host_config_init();

    xTaskCreate(nimble_host_task, "NimBLE Host", 4 * 1024, NULL, 5, NULL);
    xTaskCreate(battery_level_task, "Battery Level", 4 * 1024, NULL, 5, NULL);
}
