#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"

void wifi_manager_init(void);
esp_err_t wifi_manager_load_config(void);
esp_err_t wifi_manager_start(void);
void wifi_manager_stop(void);
const char *wifi_manager_get_current_ssid(void);
const char *wifi_manager_get_current_password(void);

esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);
esp_err_t wifi_manager_clear_credentials(void);

#endif // WIFI_MANAGER_H
