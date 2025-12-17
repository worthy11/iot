#ifndef MOTOR_DRIVER_H
#define MOTOR_DRIVER_H

#include "driver/gpio.h"

#define STEPS_PER_FULL_ROTATION 4096
// #define STEPS_PER_PORTION 256 // 16 portions
#define STEPS_PER_PORTION 512 // 8 portions

void motor_driver_init(gpio_num_t in1, gpio_num_t in2, gpio_num_t in3, gpio_num_t in4);
void motor_rotate_portion(bool direction);

#endif // MOTOR_DRIVER_H
