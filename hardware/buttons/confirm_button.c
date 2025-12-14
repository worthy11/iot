#include "button.h"
#include "confirm_button.h"
#include "hardware_manager.h"
#include "event_manager.h"

void confirm_button_init(gpio_num_t gpio)
{
    static button_config_t config = {
        .name = "confirm_button",
        .press_event_bit = EVENT_BIT_DISPLAY_CONFIRM,
        .long_press_event_bit = EVENT_BIT_FEED_SCHEDULED, // Long press for direct feed
        .debounce_ms = BUTTON_DEBOUNCE_MS,
        .long_press_ms = BUTTON_LONG_PRESS_MS,
        .task_stack_size = 2048,
        .task_priority = 5};

    config.gpio = gpio;
    button_init(&config);
}
