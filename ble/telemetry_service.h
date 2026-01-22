#pragma once

#include "host/ble_gatt.h"

const struct ble_gatt_svc_def *telemetry_service_get_svc_def(void);
void telemetry_service_notify_temperature(float temperature);
void telemetry_service_notify_ph(float ph);
void telemetry_service_notify_feed(bool success);
void telemetry_service_notify_alert(const char *event, const char *value);
