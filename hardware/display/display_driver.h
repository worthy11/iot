#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include "driver/gpio.h"

void display_init(gpio_num_t scl_gpio, gpio_num_t sda_gpio);

void display_event(const char *event, float value);
void display_update(void);
void display_wake(void);
void display_next(void);
void display_prev(void);
void display_confirm(void);

// Measurement data storage functions
void display_set_temperature(float temperature);
void display_set_ph(float ph);
void display_set_feed_time(time_t feed_time);
float display_get_temperature(void);
float display_get_ph(void);
time_t display_get_feed_time(void);

#endif // DISPLAY_DRIVER_H
