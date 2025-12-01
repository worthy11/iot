#pragma once

#include "host/ble_gatt.h"
#include "host/ble_gap.h"

int wifi_config_service_init(void);
const struct ble_gatt_svc_def *wifi_config_service_get_svc_def(void);
void wifi_config_service_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
void wifi_config_service_subscribe_cb(struct ble_gap_event *event);
