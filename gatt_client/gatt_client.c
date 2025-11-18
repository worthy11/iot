#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_console.h"
#include "driver/uart.h"

#include "gatt_client.h"
#include "manager.h"

static const char *TAG = "gatt_client";

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

static int cmd_alert(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: alert <0|1|2>\n");
        printf("  0 = No alert\n");
        printf("  1 = Mild alert\n");
        printf("  2 = High alert\n");
        return -1;
    }

    uint8_t level = (uint8_t)atoi(argv[1]);
    if (level > 2)
    {
        printf("Invalid alert level. Use 0, 1, or 2\n");
        return -1;
    }

    int rc = gatt_client_write_alert_level(level);
    if (rc == 0)
    {
        printf("Alert level %d write initiated\n", level);
    }
    else
    {
        printf("Failed to write alert level: %d\n", rc);
    }
    return rc;
}

static int cmd_notifications(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("Usage: notifications <on|off>\n");
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

    int rc = gatt_client_set_notifications(enable);
    if (rc == 0)
    {
        printf("Notifications %s initiated\n", enable ? "enabled" : "disabled");
    }
    else
    {
        printf("Failed to set notifications: %d\n", rc);
    }
    return rc;
}

void gatt_client_main(void)
{
    const esp_console_cmd_t cmd_battery_cfg = {
        .command = "battery",
        .help = "Read battery level from connected device",
        .hint = NULL,
        .func = &cmd_battery,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_battery_cfg));

    const esp_console_cmd_t cmd_alert_cfg = {
        .command = "alert",
        .help = "Write alert level (usage: alert <0|1|2>)",
        .hint = NULL,
        .func = &cmd_alert,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_alert_cfg));

    const esp_console_cmd_t cmd_notifications_cfg = {
        .command = "notifications",
        .help = "Enable/disable tx power notifications (usage: notifications <on|off>)",
        .hint = NULL,
        .func = &cmd_notifications,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_notifications_cfg));

    ESP_LOGI(TAG, "Console ready. Commands:");
    ESP_LOGI(TAG, "  mode <client|server> - Start GATT client or server");
    ESP_LOGI(TAG, "  battery - Read battery level");
    ESP_LOGI(TAG, "  alert <0|1|2> - Write alert level");
    ESP_LOGI(TAG, "  notifications <on|off> - Enable/disable tx power notifications");

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}