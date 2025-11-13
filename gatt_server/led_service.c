
#include "common.h"
#include "led_service.h"

#ifndef CONFIG_BLINK_GPIO
#undef BLINK_GPIO
#define BLINK_GPIO 2
#endif

static const char *TAG = "led_service";

uint8_t get_led_state(void) {
    int gpio_level = gpio_get_level(BLINK_GPIO);
    int led_is_on = LED_ACTIVE_LOW ? (gpio_level == 0) : (gpio_level != 0);
    return led_is_on ? 1 : 0;
}

void led_on(void) {
    int level = LED_ACTIVE_LOW ? 0 : 1;
    gpio_set_level(BLINK_GPIO, level);
    ESP_LOGI(TAG, "led_on() set gpio %d -> level=%d", BLINK_GPIO, level);
}

void led_off(void) {
    int level = LED_ACTIVE_LOW ? 1 : 0;
    gpio_set_level(BLINK_GPIO, level);
    ESP_LOGI(TAG, "led_off() set gpio %d -> level=%d", BLINK_GPIO, level);
}

void led_init(void) {
    ESP_LOGI(TAG, "configured gpio led on pin %d", BLINK_GPIO);
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    int init_level = LED_ACTIVE_LOW ? 1 : 0;
    gpio_set_level(BLINK_GPIO, init_level);
    ESP_LOGI(TAG, "led_init() set gpio %d -> level=%d (LED_ACTIVE_LOW=%d)", BLINK_GPIO, init_level, LED_ACTIVE_LOW);
}


