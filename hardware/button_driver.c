#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "button_driver.h"
#include "event_manager.h"

static const char *TAG = "button_driver";

static void button_task(void *pvParameters)
{
    bool last_state = 1; // Button is active low (pulled up)
    bool current_state;
    bool button_pressed = false;
    bool long_press_detected = false;
    TickType_t last_press_time = 0;
    TickType_t stable_time = 0;
    TickType_t press_start_time = 0;

    while (1)
    {
        current_state = gpio_get_level(CFG_BUTTON_GPIO);
        TickType_t current_time = xTaskGetTickCount();

        if (last_state == 1 && current_state == 0 && !button_pressed)
        {
            stable_time = current_time;
        }

        if (current_state == 0 && !button_pressed &&
            (current_time - stable_time) >= pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS))
        {
            if (current_time - last_press_time > pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS))
            {
                last_press_time = current_time;
                press_start_time = current_time;
                button_pressed = true;
                long_press_detected = false;
            }
        }

        if (button_pressed && current_state == 0 && !long_press_detected)
        {
            if ((current_time - press_start_time) >= pdMS_TO_TICKS(BUTTON_LONG_PRESS_MS))
            {
                long_press_detected = true;
                ESP_LOGI(TAG, "Long press detected (3 seconds) - requesting WiFi credentials clear");
                event_manager_set_bits(EVENT_BIT_WIFI_CLEAR_CREDENTIALS);
            }
        }

        if (last_state == 0 && current_state == 1 && button_pressed)
        {
            if (!long_press_detected)
            {
                event_manager_set_bits(EVENT_BIT_BUTTON_PRESSED);
            }

            button_pressed = false;
            long_press_detected = false;
        }

        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(10)); // Poll every 10ms
    }
}

void button_driver_init(void)
{
    gpio_reset_pin(CFG_BUTTON_GPIO);
    gpio_set_direction(CFG_BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(CFG_BUTTON_GPIO, GPIO_PULLUP_ONLY);

    xTaskCreate(
        button_task,
        "button_task",
        2048,
        NULL,
        5,
        NULL);

    ESP_LOGI(TAG, "Button driver initialized (CFG button on GPIO %d)", CFG_BUTTON_GPIO);
}
