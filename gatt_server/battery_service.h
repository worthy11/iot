
#ifndef BATTERY_LEVEL_H
#define BATTERY_LEVEL_H

#include <stdint.h>
#include "esp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t get_battery_level(void);
void update_battery_level(void);

#ifdef __cplusplus
}
#endif

#endif 