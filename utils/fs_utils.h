#ifndef FS_UTILS_H
#define FS_UTILS_H

#include "esp_err.h"
#include <time.h>

// Filesystem paths
// Note: SPIFFS is a flat filesystem, no subdirectories supported
#define FS_BASE_PATH "/spiffs"
#define FS_MQTT_LOG_FILE "/spiffs/mqtt_log.json"

// Provisioning file paths (flat structure for SPIFFS)
#define FS_ROOT_CA_FILE "/spiffs/root_ca.pem"
#define FS_DEVICE_CERT_FILE "/spiffs/device_cert.pem"
#define FS_PRIVATE_KEY_FILE "/spiffs/private_key.pem"
#define FS_CLIENT_ID_FILE "/spiffs/client_id.txt"

// Maximum messages in log file
#define MAX_LOG_MESSAGES 100

// Buffer sizes for loaded log entries
// Topics: MAC address (12 chars, no colons) + "/" + suffix ("temp"/"ph"/"feed"/"log")
//   Max: 12 + 1 + 4 = 17 characters, using 20 for safety
#define FS_UTILS_TOPIC_SIZE 20
// Payloads: JSON with event, value, and timestamp - max ~112 chars, using 128 for safety
#define FS_UTILS_PAYLOAD_SIZE 128

// Initialize SPIFFS filesystem
esp_err_t fs_utils_init(void);

// MQTT log functions
esp_err_t fs_utils_save_mqtt_log(const char *topic, int qos, const char *payload, char *log_id, size_t log_id_size);
esp_err_t fs_utils_load_mqtt_logs(char **topics, int **qos, char **payloads, time_t **timestamps, char ***log_ids, size_t *count);
esp_err_t fs_utils_remove_mqtt_log(const char *id);
esp_err_t fs_utils_clear_mqtt_logs(void);
size_t fs_utils_get_mqtt_log_count(void);

// Provisioning file functions
esp_err_t fs_utils_save_root_ca(const char *root_ca_pem, size_t len);
esp_err_t fs_utils_load_root_ca(char *root_ca_pem, size_t *len);
esp_err_t fs_utils_save_device_certificate(const char *device_cert_pem, size_t len);
esp_err_t fs_utils_load_device_certificate(char *device_cert_pem, size_t *len);
esp_err_t fs_utils_save_private_key(const char *private_key_pem, size_t len);
esp_err_t fs_utils_load_private_key(char *private_key_pem, size_t *len);
esp_err_t fs_utils_save_client_id(const char *client_id);
esp_err_t fs_utils_load_client_id(char *client_id, size_t client_id_len);

#endif // FS_UTILS_H
