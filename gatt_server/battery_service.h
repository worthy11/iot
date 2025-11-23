#ifndef BATTERY_SERVICE_H
#define BATTERY_SERVICE_H

#include "host/ble_gatt.h"
#include "host/ble_gap.h"

/* Battery Service functions */
int battery_service_init(void);
void battery_service_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
void battery_service_subscribe_cb(struct ble_gap_event *event);

/* Get service definition for GATT server */
const struct ble_gatt_svc_def *battery_service_get_svc_def(void);

/* Battery level functions */
uint8_t get_battery_level(void);
void update_battery_level(void);

#endif // BATTERY_SERVICE_H
