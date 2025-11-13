#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "ble_manager.h"
#include "itag_driver.h"

static const char *TAG = "itag_driver";

#define UUID_IMMEDIATE_ALERT 0x1802
#define UUID_ALERT_LEVEL 0x2A06
#define UUID_BATTERY_SERVICE 0x180F
#define UUID_BATTERY_LEVEL 0x2A19

#define ITAG_ALERT_LEVEL 2
#define ITAG_WAIT_READY 20000
#define ITAG_READ_INTERVAL_MS 5000

#define ITAG_BIT_CONNECTED BIT0
#define ITAG_BIT_SERVICES_DISCOVERED BIT1
#define ITAG_BIT_CHARS_DISCOVERED BIT2
#define ITAG_BIT_BATTERY_DISCOVERED BIT3
#define ITAG_BIT_ALERT_DISCOVERED BIT4
#define ITAG_BIT_READ_COMPLETE BIT5
#define ITAG_BIT_WRITE_COMPLETE BIT6

static ble_device_config_t s_config;
static TaskHandle_t s_read_task = NULL;
static TaskHandle_t s_alert_task = NULL;
static EventGroupHandle_t s_event_group = NULL;

static void itag_read_battery(const uint8_t *data, uint16_t len, int status)
{
    if (status != 0)
    {
        ESP_LOGW(TAG, "Battery read failed (status=%d)", status);
        xEventGroupSetBits(s_event_group, ITAG_BIT_READ_COMPLETE);
        return;
    }

    if (len > 0)
    {
        ESP_LOGI(TAG, "Battery level: %u%%", data[0]);
    }
    else
    {
        ESP_LOGI(TAG, "Battery level: <empty>");
    }

    xEventGroupSetBits(s_event_group, ITAG_BIT_READ_COMPLETE);
}

void itag_read_battery_level(void)
{
    if (ble_manager_read_char(&s_config.services[1].chars[0]) != 0)
    {
        ESP_LOGW(TAG, "Failed to read battery level");
        xEventGroupSetBits(s_event_group, ITAG_BIT_READ_COMPLETE);
    }
}

static void itag_read_task(void *param)
{
    (void)param;

    const TickType_t read_delay = pdMS_TO_TICKS(ITAG_READ_INTERVAL_MS);
    const TickType_t timeout = pdMS_TO_TICKS(5000);

    while (1)
    {
        EventBits_t bits = xEventGroupWaitBits(
            s_event_group,
            ITAG_BIT_CONNECTED | ITAG_BIT_BATTERY_DISCOVERED,
            pdFALSE, // dont clear
            pdTRUE,  // wait for all
            portMAX_DELAY);

        if ((bits & (ITAG_BIT_CONNECTED | ITAG_BIT_BATTERY_DISCOVERED)) ==
            (ITAG_BIT_CONNECTED | ITAG_BIT_BATTERY_DISCOVERED))
        {
            itag_read_battery_level();
            xEventGroupWaitBits(
                s_event_group,
                ITAG_BIT_READ_COMPLETE,
                pdTRUE,  // clear on exit
                pdFALSE, // wait for any
                timeout);

            vTaskDelay(read_delay);
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

static void itag_write_alert(int status)
{
    if (status != 0)
    {
        ESP_LOGW(TAG, "Alert write failed (status=%d)", status);
    }
    else
    {
        ESP_LOGI(TAG, "Alert write OK");
    }

    xEventGroupSetBits(s_event_group, ITAG_BIT_WRITE_COMPLETE);
}

void itag_trigger_beep(uint8_t level)
{
    if (ble_manager_write_char(&s_config.services[0].chars[0], &level, 1) != 0)
    {
        ESP_LOGW(TAG, "Failed to write alert level");
        xEventGroupSetBits(s_event_group, ITAG_BIT_WRITE_COMPLETE);
    }
}

static void itag_alert_task(void *param)
{
    (void)param;

    const TickType_t timeout = pdMS_TO_TICKS(5000);

    EventBits_t bits = xEventGroupWaitBits(
        s_event_group,
        ITAG_BIT_CONNECTED | ITAG_BIT_ALERT_DISCOVERED,
        pdFALSE, /* Don't clear bits */
        pdTRUE,  /* Wait for ALL bits */
        portMAX_DELAY);

    ESP_LOGI(TAG, "Connection established, alert service discovered. Waiting 30 seconds...");
    vTaskDelay(pdMS_TO_TICKS(ITAG_WAIT_READY));

    if ((bits & (ITAG_BIT_CONNECTED | ITAG_BIT_ALERT_DISCOVERED)) ==
        (ITAG_BIT_CONNECTED | ITAG_BIT_ALERT_DISCOVERED))
    {
        ESP_LOGI(TAG, "Sending alert to beeper");
        itag_trigger_beep(ITAG_ALERT_LEVEL);

        xEventGroupWaitBits(
            s_event_group,
            ITAG_BIT_WRITE_COMPLETE,
            pdTRUE,  /* Clear bit on exit */
            pdFALSE, /* Wait for ANY bit */
            timeout);
    }
    else
    {
        ESP_LOGW(TAG, "Device not ready for alert (connected=%d, alert_discovered=%d)",
                 (bits & ITAG_BIT_CONNECTED) != 0,
                 (bits & ITAG_BIT_ALERT_DISCOVERED) != 0);
    }

    ESP_LOGI(TAG, "Alert task finished");
    s_alert_task = NULL;
    vTaskDelete(NULL);
}

void itag_driver_init(void)
{
    ESP_LOGI(TAG, "Initializing iTag driver...");
    s_event_group = xEventGroupCreate();

    static ble_char_config_t alert_chars[] = {
        {.uuid = UUID_ALERT_LEVEL, .bit = ITAG_BIT_ALERT_DISCOVERED, .handle = 0, .read_cb = NULL, .write_cb = itag_write_alert}};

    static ble_char_config_t battery_chars[] = {
        {.uuid = UUID_BATTERY_LEVEL, .bit = ITAG_BIT_BATTERY_DISCOVERED, .handle = 0, .read_cb = itag_read_battery, .write_cb = NULL}};

    static ble_svc_config_t services[] = {
        {.uuid = UUID_IMMEDIATE_ALERT, .n_chars = 1, .chars = alert_chars},
        {.uuid = UUID_BATTERY_SERVICE, .n_chars = 1, .chars = battery_chars}};

    s_config = (ble_device_config_t){
        .name = "iTAG",
        .services = services,
        .n_services = 2,
        .event_group = s_event_group,
        .bit_connected = ITAG_BIT_CONNECTED,
        .bit_read_complete = ITAG_BIT_READ_COMPLETE,
        .bit_write_complete = ITAG_BIT_WRITE_COMPLETE,
    };

    static const ble_device_driver_t driver = {
        .name = "iTag",
        .config = &s_config,
    };

    if (ble_manager_register_device(&driver) != 0)
    {
        ESP_LOGE(TAG, "Failed to register iTag driver");
        return;
    }
    ESP_LOGI(TAG, "iTag driver registered successfully");

    BaseType_t created = xTaskCreate(itag_read_task,
                                     "itag_read",
                                     2048,
                                     NULL,
                                     tskIDLE_PRIORITY + 1,
                                     &s_read_task);
    if (created != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create read task");
        s_read_task = NULL;
    }

    created = xTaskCreate(itag_alert_task,
                          "itag_alert",
                          4096,
                          NULL,
                          tskIDLE_PRIORITY + 1,
                          &s_alert_task);
    if (created != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create alert task");
        s_alert_task = NULL;
    }

    ESP_LOGI(TAG, "iTag driver initialized");
}