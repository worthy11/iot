#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "host/ble_hs.h"

char *normalize_name(const uint8_t *src, size_t len);
char *ble_addr_to_str(const ble_addr_t *addr);
bool is_addr_empty(const ble_addr_t *addr);
