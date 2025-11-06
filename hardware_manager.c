#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "hardware_manager.h"
#include "wifi_manager.h"

#define BLINK_GPIO 2
#define BLINK_PERIOD_MS 500

static const char *TAG = "hardware_manager";
static TaskHandle_t led_task_handle = NULL;

static void led_blink_task(void *pvParameter)
{
    bool led_state = false;

    while (1)
    {
        bool connected = wifi_manager_is_connected();

        if (!connected)
        {
            led_state = !led_state;
            gpio_set_level(BLINK_GPIO, led_state);
            vTaskDelay(pdMS_TO_TICKS(BLINK_PERIOD_MS));
        }
        else
        {
            gpio_set_level(BLINK_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(1000));
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