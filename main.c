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
#include "gatt_server/hid_keyboard_service.h"
#include "mqtt/mqtt_publisher.h"
#include "hardware_manager.h"

static const char *TAG = "main";

static int cmd_mode(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: mode <client|server|mqtt>\n");
        return -1;
    }

    if (strcmp(argv[1], "client") == 0)
    {
        printf("Starting GATT client...\n");
        start_gatt_client();
    }
    else if (strcmp(argv[1], "server") == 0)
    {
        printf("Starting GATT server...\n");
        start_gatt_server();
    }
    else if (strcmp(argv[1], "mqtt") == 0)
    {
        printf("Starting MQTT publisher...\n");
        init_mqtt();
    }
    else
    {
        printf("Invalid mode. Use 'client', 'server', or 'mqtt'\n");
        return -1;
    }

    return 0;
}

static int cmd_keyboard(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: keyboard <text>\n");
        return -1;
    }

    /* Combine all arguments into a single string */
    int total_len = 0;
    for (int i = 1; i < argc; i++)
    {
        total_len += strlen(argv[i]);
        if (i < argc - 1)
        {
            total_len++; /* Space between arguments */
        }
    }

    char *text = malloc(total_len + 1);
    if (text == NULL)
    {
        printf("Failed to allocate memory for text\n");
        return -1;
    }

    text[0] = '\0';
    for (int i = 1; i < argc; i++)
    {
        strcat(text, argv[i]);
        if (i < argc - 1)
        {
            strcat(text, " ");
        }
    }

    keyboard_set_text(text);
    printf("Keyboard text queued: \"%s\"\n", text);
    free(text);
    return 0;
}

static int cmd_battery(int argc, char **argv)
{
    int rc = gatt_client_read_battery();
    if (rc == 0)
    {
        printf("Battery read initiated\n");
    }
    else
    {
        printf("Failed to read battery: %d\n", rc);
    }
    return rc;
}

static int cmd_notifs(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: notifs <on|off>\n");
        return -1;
    }

    uint8_t enable = 0;
    if (strcmp(argv[1], "on") == 0)
    {
        enable = 1;
    }
    else if (strcmp(argv[1], "off") == 0)
    {
        enable = 0;
    }
    else
    {
        printf("Invalid argument. Use 'on' or 'off'\n");
        return -1;
    }

    int rc = gatt_client_subscribe_keyboard(enable);
    if (rc == 0)
    {
        printf("Keyboard notifications %s\n", enable ? "enabled" : "disabled");
    }
    else
    {
        printf("Failed to %s keyboard notifications: %d\n", enable ? "enable" : "disable", rc);
    }
    return rc;
}

static int cmd_device_name(int argc, char **argv)
{
    if (argc < 2)
    {
        int rc = gatt_client_read_device_name();
        if (rc == 0)
        {
            printf("Device name read initiated\n");
        }
        else
        {
            printf("Failed to read device name: %d\n", rc);
        }
        return rc;
    }

    int rc = gatt_client_write_device_name(argv[1]);
    if (rc == 0)
    {
        printf("Device name write initiated: %s\n", argv[1]);
    }
    else
    {
        printf("Failed to write device name: %d\n", rc);
    }
    return rc;
}

static int cmd_appearance(int argc, char **argv)
{
    int rc = gatt_client_read_appearance();
    if (rc == 0)
    {
        printf("Appearance read initiated\n");
    }
    else
    {
        printf("Failed to read appearance: %d\n", rc);
    }
    return rc;
}

static int cmd_ppcp(int argc, char **argv)
{
    int rc = gatt_client_read_ppcp();
    if (rc == 0)
    {
        printf("PPCP read initiated\n");
    }
    else
    {
        printf("Failed to read PPCP: %d\n", rc);
    }
    return rc;
}

static int cmd_protocol_mode(int argc, char **argv)
{
    if (argc < 2)
    {
        int rc = gatt_client_read_protocol_mode();
        if (rc == 0)
        {
            printf("Protocol mode read initiated\n");
        }
        else
        {
            printf("Failed to read protocol mode: %d\n", rc);
        }
        return rc;
    }

    uint8_t mode = 0;
    if (strcmp(argv[1], "boot") == 0 || strcmp(argv[1], "0") == 0)
    {
        mode = 0;
    }
    else if (strcmp(argv[1], "report") == 0 || strcmp(argv[1], "1") == 0)
    {
        mode = 1;
    }
    else
    {
        printf("Invalid argument. Use 'boot', 'report', '0', or '1'\n");
        printf("  boot/0 = Boot Protocol (standard 8-byte reports)\n");
        printf("  report/1 = Report Protocol (custom reports)\n");
        return -1;
    }

    int rc = gatt_client_write_protocol_mode(mode);
    if (rc == 0)
    {
        printf("Protocol mode set to %s\n", mode == 0 ? "Boot Protocol" : "Report Protocol");
    }
    else
    {
        printf("Failed to set protocol mode: %d\n", rc);
    }
    return rc;
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

    const esp_console_cmd_t cmd_mode_cfg = {
        .command = "mode",
        .help = "Start GATT client, server, or MQTT publisher (usage: mode <client|server|mqtt>)",
        .hint = NULL,
        .func = &cmd_mode,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_mode_cfg));

    const esp_console_cmd_t cmd_keyboard_cfg = {
        .command = "keyboard",
        .help = "Set text for keyboard service (usage: keyboard <text>)",
        .hint = "<text>",
        .func = &cmd_keyboard,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_keyboard_cfg));

    const esp_console_cmd_t cmd_battery_cfg = {
        .command = "battery",
        .help = "Read battery level from connected device",
        .hint = NULL,
        .func = &cmd_battery,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_battery_cfg));

    const esp_console_cmd_t cmd_notif_cfg = {
        .command = "notifs",
        .help = "Enable/disable keyboard notifications (usage: notifs <on|off>)",
        .hint = NULL,
        .func = &cmd_notifs,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_notif_cfg));

    const esp_console_cmd_t cmd_protocol_mode_cfg = {
        .command = "protocol",
        .help = "Read/set HID protocol mode (usage: protocol [boot|report|0|1])",
        .hint = NULL,
        .func = &cmd_protocol_mode,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_protocol_mode_cfg));

    const esp_console_cmd_t cmd_device_name_cfg = {
        .command = "devicename",
        .help = "Read/write device name (usage: devicename [name])",
        .hint = NULL,
        .func = &cmd_device_name,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_device_name_cfg));

    const esp_console_cmd_t cmd_appearance_cfg = {
        .command = "appearance",
        .help = "Read device appearance",
        .hint = NULL,
        .func = &cmd_appearance,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_appearance_cfg));

    const esp_console_cmd_t cmd_ppcp_cfg = {
        .command = "ppcp",
        .help = "Read peripheral preferred connection parameters",
        .hint = NULL,
        .func = &cmd_ppcp,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_ppcp_cfg));

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

    ESP_LOGI(TAG, "Console ready. Commands:");
    ESP_LOGI(TAG, "  mode <client|server|mqtt> - Start GATT client or server");
    ESP_LOGI(TAG, "  battery - Read battery level");
    ESP_LOGI(TAG, "  notifs <on|off> - Enable/disable keyboard notifications");
    ESP_LOGI(TAG, "  protocol [boot|report|0|1] - Read/set HID protocol mode");
    ESP_LOGI(TAG, "  devicename [name] - Read/write device name");
    ESP_LOGI(TAG, "  appearance - Read device appearance");
    ESP_LOGI(TAG, "  ppcp - Read peripheral preferred connection parameters");
    ESP_LOGI(TAG, "  keyboard <text> - Type a message to be sent by the keyboard");

    init_hardware();
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}