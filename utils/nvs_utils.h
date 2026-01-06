#ifndef NVS_UTILS_H
#define NVS_UTILS_H

#include "esp_err.h"
#include <stdbool.h>

// WiFi credentials NVS functions
#define WIFI_CONFIG_NAMESPACE "wifi_cfg"
esp_err_t nvs_save_wifi_credentials(const char *ssid, const char *password);
esp_err_t nvs_clear_wifi_credentials(void);
esp_err_t nvs_read_wifi_credentials(char *ssid, size_t ssid_len, char *password, size_t password_len);

// Certificate NVS functions
#define CERT_NAMESPACE "certs"
esp_err_t nvs_save_root_ca(const char *root_ca_pem);
esp_err_t nvs_save_device_certificate(const char *device_cert_pem);
esp_err_t nvs_save_private_key(const char *private_key_pem);
esp_err_t nvs_read_root_ca(char *root_ca_pem, size_t root_ca_len);
esp_err_t nvs_read_device_certificate(char *device_cert_pem, size_t device_cert_len);
esp_err_t nvs_read_private_key(char *private_key_pem, size_t private_key_len);

// Provisioning NVS functions
#define PROVISIONING_NAMESPACE "provisioning"
esp_err_t nvs_save_topic_id(const char *topic_id);
esp_err_t nvs_read_topic_id(char *topic_id, size_t topic_id_len);
bool nvs_topic_id_changed(const char *new_topic_id);

#endif // NVS_UTILS_H
