#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hardware/oled_manager.h"

extern TaskHandle_t led_task_handle;

#define BLINK_GPIO 2
#define BLINK_PERIOD_MS 500

#define OLED_SCL_GPIO 22
#define OLED_SDA_GPIO 21

void init_hardware();

#endif