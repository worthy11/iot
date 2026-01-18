#pragma once

#include "host/ble_gatt.h"

const struct ble_gatt_svc_def *command_service_get_svc_def(void);
const char *command_service_get_firmware_url(void);
