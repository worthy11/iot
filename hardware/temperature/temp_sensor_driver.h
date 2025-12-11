#pragma once

#include "driver/gpio.h"
#include <stdint.h>

// Initialize DS18B20 on the given GPIO (e.g., GPIO_NUM_4 for D4)
void temp_sensor_init(gpio_num_t pin);

// Read temperature in Celsius; returns true on success
bool temp_sensor_read_celsius(float *out_celsius);


 

