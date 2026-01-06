#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include "driver/gpio.h"

void display_init(gpio_num_t scl_gpio, gpio_num_t sda_gpio);
void display_interrupt(void);
void display_interrupt_with_value(float value, bool is_temp);
void display_update(void);
void display_wake(void);
void display_next(void);
void display_prev(void);
void display_confirm(void);

#endif // DISPLAY_DRIVER_H
