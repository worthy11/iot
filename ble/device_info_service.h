#ifndef DEVICE_INFO_SERVICE_H
#define DEVICE_INFO_SERVICE_H

#include "host/ble_gatt.h"
#include "host/ble_gap.h"

/* Get service definition */
const struct ble_gatt_svc_def *device_info_service_get_svc_def(void);

/* Initialize service */
int device_info_service_init(void);

/* Register callback */
void device_info_service_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);

/* Subscribe callback */
void device_info_service_subscribe_cb(struct ble_gap_event *event);

#endif /* DEVICE_INFO_SERVICE_H */
