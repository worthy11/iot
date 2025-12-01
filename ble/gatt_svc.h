#ifndef GATT_SVR_H
#define GATT_SVR_H

#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_gap.h"

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
void gatt_svr_subscribe_cb(struct ble_gap_event *event);
int gatt_svc_init(void);
int gatt_svr_send_keyboard_report(uint8_t modifiers, uint8_t *key_codes, uint8_t key_count);

#endif // GATT_SVR_H
