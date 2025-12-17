#ifndef BREAK_BEAM_H
#define BREAK_BEAM_H

#include "driver/gpio.h"
#include <stdbool.h>

void break_beam_init(gpio_num_t gpio);
void break_beam_monitor(void *pvParameters);
int break_beam_get_state(void); // Returns 0 if beam broken, 1 if intact

#endif // BREAK_BEAM_H