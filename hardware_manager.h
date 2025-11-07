#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern TaskHandle_t led_task_handle;

void init_hardware();

#endif