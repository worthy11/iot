#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "wifi_manager.h"
#include "protocol_manager.h"
#include "hardware_manager.h"
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "main";
const char *HOST = "imgur.com";
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

    init_hardware();
    init_wifi_manager();

    // zmienic logike mrugania tak, zeby byla odpalana w innym tasku
    // prawdopodobnie funkcja xTaskCreate

    // trzeba zmienic te zmienna na cos innego co moze byc widziane przez
    // rozne taski jednoczesnie
    bool wifi_connected = false;

    bool request_successful = false;

    while (1)
    {
        if (!wifi_connected)
        {
            ESP_LOGI(TAG, "Not connected to wifi!");
        }
        else
        {
            ESP_LOGI(TAG, "Connected to wifi!");
            printf("%s\n", request_successful ? "True" : "False");

            if (!request_successful)
            {
                int sock = tcp_connector(HOST, PORT);
                char *response = http_get(sock, HOST, PATH);

                if (response != NULL)
                {
                    printf("Printing response:\n");
                    printf("%s\n", response);
                    free(response);
                    request_successful = true;
                }

                tcp_disconnect(sock);
            }
        }
        wifi_connected = wifi_manager_is_connected();

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}