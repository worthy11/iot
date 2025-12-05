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
#include "hardware/ssd1306_demo.h"

static const char *TAG = "main";

static int cmd_demo(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Running OLED demo...\n");
    ssd1306_demo_run();
    return 0;
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize console REPL
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp32>";
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ret = esp_console_new_repl_uart(&hw_config, &repl_config, &repl);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create REPL: %s (0x%x)", esp_err_to_name(ret), ret);
        return;
    }

    // Register demo command
    const esp_console_cmd_t cmd_demo_cfg = {
        .command = "demo",
        .help = "Run OLED API showcase demo",
        .hint = NULL,
        .func = &cmd_demo,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_demo_cfg));

    // Start REPL
    ret = esp_console_start_repl(repl);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start REPL: %s (0x%x)", esp_err_to_name(ret), ret);
        return;
    }

    ESP_LOGI(TAG, "Console ready. Type 'demo' to run OLED showcase.");

    event_manager_init();
    init_wifi_manager();
    gatt_server_manager_init();
    init_hardware();
}
