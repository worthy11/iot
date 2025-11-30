#ifndef HID_KEYBOARD_SERVICE_H
#define HID_KEYBOARD_SERVICE_H

#include <string.h>
#include "nimble/ble.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"

/* HID Keyboard Service functions */
int hid_keyboard_service_init(void);
void hid_keyboard_service_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
void hid_keyboard_service_subscribe_cb(struct ble_gap_event *event);
int hid_keyboard_service_send_report(uint8_t modifiers, uint8_t *key_codes, uint8_t key_count);

/* Get service definition for GATT server */
const struct ble_gatt_svc_def *hid_keyboard_service_get_svc_def(void);

extern const ble_uuid16_t keyboard_svc_uuid;
extern const ble_uuid16_t keyboard_chr_uuid;

extern uint16_t keyboard_chr_val_handle;

void keyboard_set_text(const char *txt);

int keyboard_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg);

extern uint16_t keyboard_conn_handle;
extern bool keyboard_notify_enabled;

#endif