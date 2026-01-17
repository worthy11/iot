#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

void ble_manager_init(void);
void ble_start_advertising(void);
void ble_stop_advertising(void);
uint32_t ble_manager_get_passkey(void);
void ble_manager_notify_temperature(float temperature);
void ble_manager_notify_ph(float ph);
void ble_manager_notify_feed(bool success);

#endif // BLE_MANAGER_H