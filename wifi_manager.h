#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_STATUS_BIT BIT0

extern EventGroupHandle_t wifi_status_event_group;

void init_wifi_manager(void);
bool wifi_manager_is_connected(void);

#endif // WIFI_MANAGER_H
