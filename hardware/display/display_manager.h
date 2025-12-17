#ifndef SSD1306_DEMO_H
#define SSD1306_DEMO_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include <stdint.h>

void display_init(gpio_num_t scl_gpio, gpio_num_t sda_gpio);
void display_manager_update_display(void);

#endif // SSD1306_DEMO_H
