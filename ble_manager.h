#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <stdint.h>

void init_ble_manager(void);
void ble_manager_trigger_beep(uint8_t level);

#endif