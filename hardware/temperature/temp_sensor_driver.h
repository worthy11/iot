#pragma once

#include "driver/gpio.h"
#include <stdint.h>

void temp_sensor_init(gpio_num_t pin);
// Read temperature in Celsius; returns true on success
bool temp_sensor_read_celsius(float *out_celsius);


 

