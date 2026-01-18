#ifndef HTTP_MANAGER_H
#define HTTP_MANAGER_H

#include "esp_err.h"

esp_err_t http_manager_perform_ota_update(const char *firmware_url, const char *firmware_version);

#endif // HTTP_MANAGER_H
