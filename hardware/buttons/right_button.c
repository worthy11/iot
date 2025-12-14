#include "button.h"
#include "right_button.h"
#include "hardware_manager.h"
#include "event_manager.h"

void right_button_init(gpio_num_t gpio)
{
    static button_config_t config = {
        .name = "right_button",
        .press_event_bit = EVENT_BIT_DISPLAY_RIGHT,
        .long_press_event_bit = 0, // No long press
        .debounce_ms = BUTTON_DEBOUNCE_MS,
        .long_press_ms = 0, // Disable long press
        .task_stack_size = 2048,
        .task_priority = 5};

    config.gpio = gpio;
    button_init(&config);
}
