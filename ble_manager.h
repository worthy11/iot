#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/event_groups.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"

#define BLE_DEVICE_MAX_SERVICES 8
#define BLE_DEVICE_MAX_CHARS_PER_SERVICE 8
#define BLE_DEVICE_MAX_TOTAL_CHARS 32

typedef void (*ble_read_cb_t)(const uint8_t *data, uint16_t len, int status);
typedef void (*ble_write_cb_t)(int status);

typedef struct
{
    uint16_t uuid;
    uint32_t bit;
    uint16_t handle; /* GATT value handle (0 = not discovered) */
    ble_read_cb_t read_cb;
    ble_write_cb_t write_cb;
} ble_char_config_t;

typedef struct
{
    uint16_t uuid;
    uint8_t n_chars;
    ble_char_config_t *chars;
} ble_svc_config_t;

typedef struct
{
    const char *name;
    ble_svc_config_t *services;
    uint8_t n_services;
    EventGroupHandle_t event_group;
    uint32_t bit_connected;
    uint32_t bit_read_complete;
    uint32_t bit_write_complete;
} ble_device_config_t;

typedef struct
{
    const char *name;
    uint16_t conn_handle;
    ble_device_config_t *config;
} ble_device_driver_t;

void init_ble_manager(void);
int ble_manager_register_device(const ble_device_driver_t *driver);
int ble_manager_read_char(ble_char_config_t *char_config);
int ble_manager_write_char(ble_char_config_t *char_config, const uint8_t *data, uint16_t len);
int ble_manager_enable_notify(ble_char_config_t *char_config);

#endif
