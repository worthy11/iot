#ifndef KEYBOARD_SERVICE_H
#define KEYBOARD_SERVICE_H

#include <string.h>
#include "nimble/ble.h"
#include "host/ble_gatt.h"

extern const ble_uuid16_t keyboard_svc_uuid;
extern const ble_uuid16_t keyboard_chr_uuid;

extern uint16_t keyboard_chr_val_handle;

void keyboard_set_text(const char *txt);

int keyboard_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg);


extern uint16_t keyboard_conn_handle;
extern bool keyboard_notify_enabled;

#endif