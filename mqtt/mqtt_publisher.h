#ifndef MQTT_PUBLISHER_H
#define MQTT_PUBLISHER_H

/**
 * @brief Initialize MQTT publisher
 * 
 * Initializes NVS, network interface, event loop, connects to WiFi,
 * and starts the MQTT client with message publishing capability.
 */
void init_mqtt(void);

#endif // MQTT_PUBLISHER_H
