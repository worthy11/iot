#ifndef GATT_SVR_H
#define GATT_SVR_H

#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"

#include "host/ble_gap.h"

void send_battery_level_indication(void);
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
void gatt_svr_subscribe_cb(struct ble_gap_event *event);
int gatt_svc_init(void);


#endif 