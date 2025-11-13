
#include "common.h"
#include "battery_service.h"

static uint8_t battery_level;

uint8_t get_battery_level(void) { return battery_level; }

void update_battery_level(void) { battery_level = battery_level > 0 ? battery_level - 0.1 : 100; }