#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "hardware_manager.h"
#include "wifi_manager.h"

#define BLINK_GPIO 2
#define BLINK_PERIOD_MS 500

static const char *TAG = "hardware_manager";
TaskHandle_t led_task_handle = NULL;

static void led_blink_task(void *pvParameters)
{
    uint32_t notification = 0;

    led_task_handle = xTaskGetCurrentTaskHandle();

    while (1)
    {
        if (wifi_status_event_group == NULL)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        EventBits_t bits = xEventGroupGetBits(wifi_status_event_group);
        if (!(bits & WIFI_STATUS_BIT))
        {
            gpio_set_level(BLINK_GPIO, 1);
            xTaskNotifyWait(0, UINT32_MAX, &notification, pdMS_TO_TICKS(BLINK_PERIOD_MS));
            if (notification)
            {
                notification = 0;
                continue;
            }

            gpio_set_level(BLINK_GPIO, 0);
            xTaskNotifyWait(0, UINT32_MAX, &notification, pdMS_TO_TICKS(BLINK_PERIOD_MS));
            if (notification)
            {
                notification = 0;
                continue;
            }
        }
        else
        {
            gpio_set_level(BLINK_GPIO, 0);
            xTaskNotifyWait(0, UINT32_MAX, &notification, portMAX_DELAY);
            notification = 0;
            continue;
        }
    }
}

void init_hardware(void)
{
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BLINK_GPIO, 0);

    if (led_task_handle == NULL)
    {
        xTaskCreate(
            led_blink_task,
            "led_blink_task",
            2048,
            NULL,
            5,
            &led_task_handle);
        ESP_LOGI(TAG, "LED blink task started");
    }
}