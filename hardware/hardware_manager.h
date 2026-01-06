#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"

#include "buttons/left_button.h"
#include "buttons/confirm_button.h"
#include "buttons/right_button.h"
#include "feeder/beam_driver.h"
#include "feeder/motor_driver.h"
#include "ph/ph_sensor_driver.h"
#include "temperature/temp_sensor_driver.h"

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
#define GPIO_BREAK_BEAM 23

// Stepper Motor
#define GPIO_MOTOR_IN1 16
#define GPIO_MOTOR_IN2 17
#define GPIO_MOTOR_IN3 18
#define GPIO_MOTOR_IN4 19
#define GPIO_MOTOR_RETRY_DELAY_MS 500

// pH Sensor
#define GPIO_PH_OUTPUT 32
#define GPIO_PH_TEMP_COMP 33
#define GPIO_PH_POWER 25

// Power stabilization delay for pH sensor (ms)
#define PH_POWER_STABILIZE_MS 100

// OLED Display
#define GPIO_OLED_SDA 21
#define GPIO_OLED_SCL 22

// Temperature Sensor
#define GPIO_TEMP_SENSOR 4

void hardware_manager_init(void);

void hardware_manager_display_interrupt(void);
void hardware_manager_display_interrupt_with_value(float value, bool is_temp);
void hardware_manager_display_update(void);
void hardware_manager_display_wake(void);
void hardware_manager_display_next(void);
void hardware_manager_display_prev(void);
void hardware_manager_display_confirm(void);

float hardware_manager_measure_temp(void);
float hardware_manager_measure_ph(void);

void hardware_manager_set_temp_reading_interval(uint32_t interval_seconds);
void hardware_manager_set_feeding_interval(uint32_t interval_seconds);

void hardware_manager_motor_rotate_portion(bool direction);
void hardware_manager_start_beam_monitor(TaskHandle_t *task_handle);
void hardware_manager_stop_beam_monitor(TaskHandle_t task_handle);

#endif