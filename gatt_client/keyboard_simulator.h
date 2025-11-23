#ifndef KEYBOARD_SIMULATOR_H
#define KEYBOARD_SIMULATOR_H

#include <stdint.h>
#include <stdbool.h>

void keyboard_simulator_init(void);
void keyboard_simulator_set_enabled(bool enabled);
void keyboard_simulator_process_report(const uint8_t *data, uint16_t len);

#endif /* KEYBOARD_SIMULATOR_H */