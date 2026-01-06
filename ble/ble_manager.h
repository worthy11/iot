#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <stdint.h>

void ble_manager_init(void);
void ble_start_advertising(void);
void ble_stop_advertising(void);
uint32_t ble_manager_get_passkey(void);

#endif // BLE_MANAGER_H