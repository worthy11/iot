#ifndef NVS_UTILS_H
#define NVS_UTILS_H

#include "esp_err.h"
#include <stdbool.h>
#include <time.h>

esp_err_t nvs_save_blob(const char *namespace, const char *key, const void *value, size_t len);
esp_err_t nvs_load_blob(const char *namespace, const char *key, void *value, size_t *len);

#endif // NVS_UTILS_H
