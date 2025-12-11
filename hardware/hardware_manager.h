#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

// Buttons
#define GPIO_CONFIG_BUTTON 13
#define GPIO_FEED_BUTTON 15
#define GPIO_DISPLAY_BUTTON 14
#define BUTTON_DEBOUNCE_MS 50
#define BUTTON_LONG_PRESS_MS 3000

// Break Beam Sensor
#define GPIO_BREAK_BEAM 5

// Stepper Motor
#define GPIO_MOTOR_IN1 15
#define GPIO_MOTOR_IN2 16
#define GPIO_MOTOR_IN3 17
#define GPIO_MOTOR_IN4 18

// pH Sensor
#define GPIO_PH_OUTPUT 32
#define GPIO_PH_TEMP_COMP 33

// OLED Display
#define GPIO_OLED_SCL 22
#define GPIO_OLED_SDA 21

// Temperature Sensor (1-Wire) - Add when implemented
// #define GPIO_TEMP_SENSOR      XX

#include "buttons/config_button.h"
#include "buttons/feed_button.h"
#include "buttons/display_button.h"
#include "display/display_manager.h"
#include "feeder/beam_driver.h"
#include "feeder/motor_driver.h"
#include "ph/ph_sensor_driver.h"
#include "temperature/temp_sensor_driver.h"

void hardware_init();

#endif