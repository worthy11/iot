#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "wifi_manager.h"
#include "http_manager.h"
#include "hardware_manager.h"
#include <stdio.h>

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_hardware();
    init_wifi_manager();

    bool wifi_was_connected = false;
    xTaskCreate(&http_get_task, "http_get_task", 4096, NULL, 5, NULL);

    while (1)
    {
        bool wifi_connected = wifi_manager_is_connected();

        if (!wifi_connected)
        {
            ESP_LOGI(TAG, "Not connected to wifi!");
            if (wifi_was_connected)
            {
                start_led_blink(500);
                ESP_LOGI(TAG, "WiFi disconnected - starting LED blink");
            }
        }
        else
        {
            ESP_LOGI(TAG, "Connected to wifi!");
            if (!wifi_was_connected)
            {
                stop_led_blink();
                ESP_LOGI(TAG, "WiFi connected - stopping LED blink");
            }
        }

        wifi_was_connected = wifi_connected;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}