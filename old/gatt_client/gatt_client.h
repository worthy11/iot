#ifndef GATT_CLIENT_H
#define GATT_CLIENT_H

#include <stdint.h>

void start_gatt_client(void);
int gatt_client_read_battery(void);
int gatt_client_subscribe_keyboard(uint8_t enable);
int gatt_client_read_protocol_mode(void);
int gatt_client_write_protocol_mode(uint8_t mode);
int gatt_client_read_device_name(void);
int gatt_client_write_device_name(const char *name);
int gatt_client_read_appearance(void);
int gatt_client_read_ppcp(void);

#endif /* GATT_CLIENT_H */