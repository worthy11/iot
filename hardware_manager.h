#ifndef HARDWARE_MANAGER_H
#define HARDWARE_MANAGER_H

#include <stdint.h>

void init_hardware(void);
void init_led(void);
void start_led_blink(uint32_t period_ms);
void stop_led_blink(void);

#endif // HARDWARE_MANAGER_H