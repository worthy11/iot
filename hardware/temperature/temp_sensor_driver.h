#pragma once

#include "driver/gpio.h"
#include <stdint.h>

void temp_sensor_init(gpio_num_t pin);
float temp_sensor_read();


 

