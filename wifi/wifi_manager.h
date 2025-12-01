#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"

#define WIFI_STATUS_BIT BIT0

extern EventGroupHandle_t wifi_status_event_group;

void init_wifi_manager(void);
bool wifi_manager_is_connected(void);
const char *wifi_manager_get_current_ssid(void);

/* Save WiFi credentials to NVS (for BLE config or testing) */
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password);

#endif // WIFI_MANAGER_H
