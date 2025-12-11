#ifndef BUTTON_H
#define BUTTON_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"

typedef struct
{
    gpio_num_t gpio;                  // GPIO pin number
    const char *name;                 // Button name for logging
    EventBits_t press_event_bit;      // Event bit to set on short press (0 if none)
    EventBits_t long_press_event_bit; // Event bit to set on long press (0 if none)
    uint32_t debounce_ms;             // Debounce time in milliseconds
    uint32_t long_press_ms;           // Long press duration in milliseconds (0 to disable)
    uint32_t task_stack_size;         // Stack size for button task
    UBaseType_t task_priority;        // Task priority
} button_config_t;

void button_init(const button_config_t *config);

#endif // BUTTON_H
