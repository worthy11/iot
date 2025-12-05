#ifndef SSD1306_DEMO_H
#define SSD1306_DEMO_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>

void ssd1306_demo_init();
void ssd1306_demo_run(void);
void ssd1306_demo_display_passkey(uint32_t passkey);
void ssd1306_demo_clear_passkey(void);

#endif // SSD1306_DEMO_H
