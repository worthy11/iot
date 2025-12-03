#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_console.h"
#include "driver/uart.h"

#include "event_manager.h"
#include "wifi/wifi_manager.h"
#include "ble/gatt_server.h"
#include "mqtt/mqtt_publisher.h"
#include "hardware/hardware_manager.h"

static const char *TAG = "main";

// static int cmd_mode(int argc, char **argv)
// {
//     if (argc < 2)
//     {
//         printf("Usage: mode <ble|mqtt>\n");
//         return -1;
//     }

//     if (strcmp(argv[1], "ble") == 0)
//     {
//         printf("Starting GATT server...\n");
//         start_gatt_server();
//     }
//     else if (strcmp(argv[1], "mqtt") == 0)
//     {
//         printf("Starting MQTT publisher...\n");
//         init_mqtt();
//     }
//     else
//     {
//         printf("Invalid mode. Use 'client', 'server', or 'mqtt'\n");
//         return -1;
//     }

//     return 0;
// }

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // const esp_console_cmd_t cmd_mode_cfg = {
    //     .command = "mode",
    //     .help = "Start GATT server or MQTT publisher (usage: mode <ble|mqtt>)",
    //     .hint = NULL,
    //     .func = &cmd_mode,
    // };
    // ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_mode_cfg));

    // esp_console_repl_t *repl = NULL;
    // esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    // repl_config.prompt = "esp32>";
    // esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    // ret = esp_console_new_repl_uart(&hw_config, &repl_config, &repl);
    // if (ret != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "Failed to create REPL: %s (0x%x)", esp_err_to_name(ret), ret);
    //     return;
    // }
    // ret = esp_console_start_repl(repl);
    // if (ret != ESP_OK)
    // {
    //     ESP_LOGE(TAG, "Failed to start REPL: %s (0x%x)", esp_err_to_name(ret), ret);
    //     return;
    // }

    // ESP_LOGI(TAG, "Console ready. Commands:");
    // ESP_LOGI(TAG, "  mode <ble|mqtt> - Start GATT server or MQTT");

    event_manager_init();
    init_wifi_manager();
    gatt_server_manager_init(); // Initialize GATT server manager (waits for button press)
    init_hardware();

    // while (1)
    // {
    //     vTaskDelay(pdMS_TO_TICKS(1000));
    // }
}