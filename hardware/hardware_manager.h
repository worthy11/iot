#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"

// Hardware manager event group bits
#define HARDWARE_BIT_FEED_SUCCESS BIT0
#define HARDWARE_BIT_FEED_FAILURE BIT1

// Buttons
#define GPIO_LEFT_BUTTON 13
#define GPIO_RIGHT_BUTTON 14
#define GPIO_CONFIRM_BUTTON 15
#define BUTTON_DEBOUNCE_MS 50
#define BUTTON_LONG_PRESS_MS 3000

// Break Beam Sensor
#define GPIO_BREAK_BEAM 5

// Stepper Motor
#define GPIO_MOTOR_IN1 16
#define GPIO_MOTOR_IN2 17
#define GPIO_MOTOR_IN3 18
#define GPIO_MOTOR_IN4 19
#define GPIO_MOTOR_RETRY_DELAY_MS 500

// pH Sensor
#define GPIO_PH_OUTPUT 32
#define GPIO_PH_TEMP_COMP 33
// pH Sensor Power Control (powers sensor only during measurement)
#define GPIO_PH_POWER 25

// Power stabilization delay for pH sensor (ms)
#define PH_POWER_STABILIZE_MS 100

// OLED Display
#define GPIO_OLED_SDA 21
#define GPIO_OLED_SCL 22

// Temperature Sensor
#define GPIO_TEMP_SENSOR 4

#include "buttons/left_button.h"
#include "buttons/confirm_button.h"
#include "buttons/right_button.h"
#include "display/display_manager.h"
#include "feeder/beam_driver.h"
#include "feeder/motor_driver.h"
#include "ph/ph_sensor_driver.h"
#include "temperature/temp_sensor_driver.h"

void hardware_init();
EventGroupHandle_t hardware_manager_get_event_group(void);

#endif