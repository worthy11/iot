#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"

#define WIFI_STATUS_BIT BIT0

extern EventGroupHandle_t wifi_status_event_group;

void wifi_manager_init(void);
const char *wifi_manager_get_current_ssid(void);
const char *wifi_manager_get_current_password(void);

esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);
esp_err_t wifi_manager_clear_credentials(void);

#endif // WIFI_MANAGER_H
