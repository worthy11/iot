#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "wifi_manager.h"
#include "protocol_manager.h"
#include "hardware_manager.h"
#include "ble_manager.h"

static const char *TAG = "main";
const char *HOST = "example.com";
const char *PORT = "80";
const char *PATH = "/";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // init_hardware();
    // init_wifi_manager();
    init_ble_manager();

    bool request_successful = false;

    while (1)
    {
        // if (!request_successful && wifi_manager_is_connected())
        // {
        //     int sock = tcp_connector(HOST, PORT);
        //     char *response = http_get(sock, HOST, PATH);

        //     if (response != NULL)
        //     {
        //         ESP_LOGI(TAG, "Request successful. Printing HTTP response");
        //         printf("%s\n", response);
        //         free(response);
        //         request_successful = true;
        //         ESP_LOGI(TAG, "HTTP response receive successful");
        //     }

        //     tcp_disconnect(sock);
        // }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}