#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "button_driver.h"
#include "event_manager.h"

static const char *TAG = "button_driver";

static void button_task(void *pvParameters)
{
    bool last_state = 1; // BOOT button is active low (pulled up)
    bool current_state;
    bool button_pressed = false;
    TickType_t last_press_time = 0;
    TickType_t stable_time = 0;

    while (1)
    {
        current_state = gpio_get_level(CFG_BUTTON_GPIO);
        TickType_t current_time = xTaskGetTickCount();

        // Detect button press (falling edge: 1 -> 0)
        if (last_state == 1 && current_state == 0 && !button_pressed)
        {
            stable_time = current_time;
        }

        // Wait for stable pressed state (debounce)
        if (current_state == 0 && !button_pressed &&
            (current_time - stable_time) >= pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS))
        {
            if (current_time - last_press_time > pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS))
            {
                last_press_time = current_time;
                button_pressed = true;
                ESP_LOGI(TAG, "Button pressed!");

                event_manager_set_bits(EVENT_BIT_BUTTON_PRESSED);
            }
        }

        // Detect button release (rising edge: 0 -> 1)
        if (last_state == 0 && current_state == 1 && button_pressed)
        {
            button_pressed = false;
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
