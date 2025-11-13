
#ifndef LED_H
#define LED_H

#include "driver/gpio.h"
#include "sdkconfig.h"

#define BLINK_GPIO CONFIG_BLINK_GPIO

#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 0
#endif

uint8_t get_led_state(void);
void led_on(void);
void led_off(void);
void led_init(void);

#endif 