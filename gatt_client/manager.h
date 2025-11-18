#pragma once

#include <stdint.h>

void start_gatt_client(void);
int gatt_client_read_battery(void);
int gatt_client_write_alert_level(uint8_t level);
int gatt_client_set_notifications(uint8_t enable);