#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_console.h"
#include "driver/uart.h"

#include "gatt_client/gatt_client.h"
#include "gatt_server/gatt_server.h"

static const char *TAG = "main";

static int cmd_mode(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: mode <client|server>\n");
        return -1;
    }

    if (strcmp(argv[1], "client") == 0)
    {
        printf("Starting GATT client...\n");
        gatt_client_main();
    }
    else if (strcmp(argv[1], "server") == 0)
    {
        printf("Starting GATT server...\n");
        gatt_server_main();
    }
    else
    {
        printf("Invalid mode. Use 'client' or 'server'\n");
        return -1;
    }

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

    // esp_console_config_t console_config = ESP_CONSOLE_CONFIG_DEFAULT();
    // ESP_ERROR_CHECK(esp_console_init(&console_config));

    const esp_console_cmd_t cmd_mode_cfg = {
        .command = "mode",
        .help = "Start GATT client or server (usage: mode <client|server>)",
        .hint = NULL,
        .func = &cmd_mode,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_mode_cfg));

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
    ret = esp_console_start_repl(repl);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start REPL: %s (0x%x)", esp_err_to_name(ret), ret);
        return;
    }

    ESP_LOGI(TAG, "Console ready. Use 'mode client' or 'mode server' to start");

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}