#include "button.h"
#include "display_button.h"
#include "hardware_manager.h"
#include "event_manager.h"

void display_button_init(gpio_num_t gpio)
{
    static button_config_t config = {
        .name = "display_button",
        .press_event_bit = EVENT_BIT_DISPLAY_STATUS,
        .long_press_event_bit = 0,
        .debounce_ms = BUTTON_DEBOUNCE_MS,
        .long_press_ms = 0,
        .task_stack_size = 2048,
        .task_priority = 5};

    config.gpio = gpio;
    button_init(&config);
}
