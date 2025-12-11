#include "button.h"
#include "config_button.h"
#include "hardware_manager.h"
#include "event_manager.h"

void config_button_init(gpio_num_t gpio)
{
    static button_config_t config = {
        .name = "config_button",
        .press_event_bit = EVENT_BIT_CONFIG_BUTTON_PRESSED,
        .long_press_event_bit = EVENT_BIT_WIFI_CLEARED,
        .debounce_ms = BUTTON_DEBOUNCE_MS,
        .long_press_ms = BUTTON_LONG_PRESS_MS,
        .task_stack_size = 2048,
        .task_priority = 5};

    config.gpio = gpio;
    button_init(&config);
}
