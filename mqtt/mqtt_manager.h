#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

void mqtt_manager_init(void);
esp_err_t mqtt_manager_load_config(void);
void mqtt_manager_start(void);
void mqtt_manager_stop(void);

int mqtt_manager_get_temp_frequency(void);
int mqtt_manager_get_feed_frequency(void);

void mqtt_manager_enqueue_temperature(float temperature);
void mqtt_manager_enqueue_ph(float ph);
void mqtt_manager_enqueue_feed(bool success);
void mqtt_manager_enqueue_log(const char *event, const char *value);
void mqtt_manager_publish(void);

#endif // MQTT_MANAGER_H
