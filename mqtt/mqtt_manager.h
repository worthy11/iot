#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

void mqtt_manager_init(void);
esp_err_t mqtt_manager_load_config(void);
void mqtt_manager_enqueue_data(const char *topic, const char *data);
int mqtt_manager_get_temp_frequency(void);
int mqtt_manager_get_feed_frequency(void);
esp_err_t mqtt_manager_start(void);
void mqtt_manager_stop(void);

#endif // MQTT_MANAGER_H
