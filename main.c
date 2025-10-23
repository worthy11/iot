#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "wifi_manager.h"
#include "http_manager.h"
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

    wifi_manager_init();

    while (1)
    {
        if (!wifi_manager_is_connected())
        {
            ESP_LOGI(TAG, "Not connected to wifi!");
        }
        else
        {
            ESP_LOGI(TAG, "Connected to wifi!");
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}