#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "hardware_manager.h"

#define BLINK_GPIO 2

static const char *TAG = "hardware_manager";
static esp_timer_handle_t led_blink_timer = NULL;
static bool led_state = false;

void init_hardware(void)
{
    init_led();
}

void init_led(void)
{
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BLINK_GPIO, 0);
}

static void led_timer_callback(void *arg)
{
    led_state = !led_state;
    gpio_set_level(BLINK_GPIO, led_state);
}

void start_led_blink(uint32_t period_ms)
{
    if (led_blink_timer != NULL)
    {
        stop_led_blink(); // Stop existing timer if running
    }

    const esp_timer_create_args_t timer_args = {
        .callback = &led_timer_callback,
        .name = "led_blink"};

    esp_timer_create(&timer_args, &led_blink_timer);
    esp_timer_start_periodic(led_blink_timer, period_ms * 1000);
    ESP_LOGI(TAG, "Started LED blinking with %lu ms period", period_ms);
}

void stop_led_blink(void)
{
    if (led_blink_timer != NULL)
    {
        esp_timer_stop(led_blink_timer);
        esp_timer_delete(led_blink_timer);
        led_blink_timer = NULL;
        gpio_set_level(BLINK_GPIO, 0);
        led_state = false;
        ESP_LOGI(TAG, "Stopped LED blinking");
    }
}