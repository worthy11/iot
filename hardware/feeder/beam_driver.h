#ifndef BREAK_BEAM_H
#define BREAK_BEAM_H

#include "driver/gpio.h"
#include <stdbool.h>

void break_beam_init(gpio_num_t gpio, gpio_num_t power_gpio);
void break_beam_monitor(void *pvParameters);
void break_beam_power_on(void);
void break_beam_power_off(void);
bool break_beam_is_sensor_working(void);

#endif // BREAK_BEAM_H