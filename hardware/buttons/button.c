#include "button.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "event_manager.h"

static const char *TAG = "button";

static void button_task(void *pvParameters)
{
    button_config_t *config = (button_config_t *)pvParameters;

    bool last_state = 1;
    bool current_state;
    bool button_pressed = false;
    bool long_press_detected = false;
    TickType_t stable_time = 0;
    TickType_t press_start_time = 0;
    TickType_t last_press_time = 0;

    ESP_LOGI(TAG, "Button task started for %s (GPIO %d)", config->name, config->gpio);

    while (1)
    {
        current_state = gpio_get_level(config->gpio);
        TickType_t current_time = xTaskGetTickCount();

        if (last_state == 1 && current_state == 0)
        {
            stable_time = current_time;
        }

        if (current_state == 0 && !button_pressed &&
            (current_time - stable_time) >= pdMS_TO_TICKS(config->debounce_ms))
        {
            if (current_time - last_press_time > pdMS_TO_TICKS(config->debounce_ms))
            {
                last_press_time = current_time;
                press_start_time = current_time;
                button_pressed = true;
                long_press_detected = false;
            }
        }

        if (button_pressed && current_state == 0 && !long_press_detected &&
            config->long_press_ms > 0 && config->long_press_event_bit != 0)
        {
            if ((current_time - press_start_time) >= pdMS_TO_TICKS(config->long_press_ms))
            {
                long_press_detected = true;
                ESP_LOGI(TAG, "%s: Long press detected", config->name);
                event_manager_set_bits(config->long_press_event_bit);
            }
        }

        if (last_state == 0 && current_state == 1 && button_pressed)
        {
            if (!long_press_detected && config->press_event_bit != 0)
            {
                ESP_LOGI(TAG, "%s: Short press detected", config->name);
                event_manager_set_bits(config->press_event_bit);
            }

            button_pressed = false;
            long_press_detected = false;
        }

        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(10)); // Poll every 10ms
    }
}

void button_init(const button_config_t *config)
{
    gpio_reset_pin(config->gpio);
    gpio_set_direction(config->gpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(config->gpio, GPIO_PULLUP_ONLY);

    BaseType_t result = xTaskCreate(
        button_task,
        config->name,
        config->task_stack_size,
        (void *)config,
        config->task_priority,
        NULL);

    ESP_LOGI(TAG, "Button driver initialized: %s (GPIO %d)", config->name, config->gpio);
}
