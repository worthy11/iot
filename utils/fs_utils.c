#include "fs_utils.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cJSON.h>
#include <time.h>
#include "esp_random.h"
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "fs_utils";

static bool fs_mounted = false;
static SemaphoreHandle_t s_spiffs_mutex = NULL;

esp_err_t fs_utils_init(void)
{
    if (fs_mounted)
    {
        return ESP_OK;
    }

    esp_vfs_spiffs_conf_t conf = {
        .base_path = FS_BASE_PATH,
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    fs_mounted = true;

    // Create SPIFFS mutex
    if (s_spiffs_mutex == NULL)
    {
        s_spiffs_mutex = xSemaphoreCreateMutex();
        if (s_spiffs_mutex == NULL)
        {
            ESP_LOGE(TAG, "Failed to create SPIFFS mutex");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "SPIFFS mutex initialized");
    }

    ESP_LOGI(TAG, "SPIFFS initialized successfully");
    return ESP_OK;
}

static void generate_id(char *id, size_t id_size)
{
    uint32_t random_bytes[4];
    for (int i = 0; i < 4; i++)
    {
        random_bytes[i] = esp_random();
    }
    snprintf(id, id_size, "%08lx-%04lx-%04lx-%04lx-%08lx%04lx",
             (unsigned long)random_bytes[0],
             (unsigned long)((random_bytes[1] >> 16) & 0xFFFF),
             (unsigned long)(random_bytes[1] & 0xFFFF),
             (unsigned long)((random_bytes[2] >> 16) & 0xFFFF),
             (unsigned long)(random_bytes[2] & 0xFFFF),
             (unsigned long)(random_bytes[3] & 0xFFFF));
}

esp_err_t fs_utils_save_mqtt_log(const char *topic, int qos, const char *payload, char *log_id, size_t log_id_size)
{
    if (!fs_mounted)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_spiffs_mutex == NULL)
    {
        ESP_LOGE(TAG, "SPIFFS mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_spiffs_mutex, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to acquire SPIFFS mutex");
        return ESP_FAIL;
    }

    // Load existing logs
    cJSON *log_array = cJSON_CreateArray();
    FILE *file = fopen(FS_MQTT_LOG_FILE, "r");
    if (file != NULL)
    {
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);

        if (file_size > 0)
        {
            char *buffer = malloc(file_size + 1);
            if (buffer != NULL)
            {
                fread(buffer, 1, file_size, file);
                buffer[file_size] = '\0';
                cJSON *existing = cJSON_Parse(buffer);
                if (existing != NULL && cJSON_IsArray(existing))
                {
                    cJSON_Delete(log_array);
                    log_array = existing;
                }
                else if (existing != NULL)
                {
                    cJSON_Delete(existing);
                }
                free(buffer);
            }
        }
        fclose(file);
    }

    cJSON *payload_json = cJSON_Parse(payload);
    if (payload_json == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse payload JSON");
        payload_json = cJSON_CreateString(payload);
    }

    cJSON *entry = cJSON_CreateObject();
    // ID removed - logs are cleared on boot anyway

    cJSON_AddStringToObject(entry, "topic", topic);
    cJSON_AddNumberToObject(entry, "qos", qos);
    cJSON_AddItemToObject(entry, "payload", payload_json);

    cJSON_AddItemToArray(log_array, entry);

    int array_size = cJSON_GetArraySize(log_array);
    if (array_size > MAX_LOG_MESSAGES)
    {
        int to_remove = array_size - MAX_LOG_MESSAGES;
        for (int i = 0; i < to_remove; i++)
        {
            cJSON_DeleteItemFromArray(log_array, 0);
        }
    }

    // Save to file
    file = fopen(FS_MQTT_LOG_FILE, "w");
    if (file == NULL)
    {
        ESP_LOGE(TAG, "Failed to open log file for writing");
        cJSON_Delete(log_array);
        if (payload_json != NULL)
        {
            cJSON_Delete(payload_json);
        }
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_FAIL;
    }

    char *json_string = cJSON_Print(log_array);
    if (json_string != NULL)
    {
        fprintf(file, "%s", json_string);
        fflush(file); // Ensure data is written to filesystem before closing
        free(json_string);
    }
    fclose(file);

    // payload_json is now owned by the entry, don't delete it here
    cJSON_Delete(log_array);

    ESP_LOGI(TAG, "Saved MQTT log entry: topic=%s", topic);
    xSemaphoreGive(s_spiffs_mutex);
    return ESP_OK;
}

esp_err_t fs_utils_load_mqtt_logs(char **topics, int **qos, char **payloads, time_t **timestamps, char ***log_ids, size_t *count)
{
    size_t free_heap_before = esp_get_free_heap_size();
    size_t largest_block_before = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    ESP_LOGI(TAG, "Loading MQTT logs - free heap: %zu bytes, largest block: %zu bytes", free_heap_before, largest_block_before);

    if (!fs_mounted)
    {
        ESP_LOGE(TAG, "Filesystem not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_spiffs_mutex == NULL)
    {
        ESP_LOGE(TAG, "SPIFFS mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_spiffs_mutex, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to acquire SPIFFS mutex");
        return ESP_FAIL;
    }

    *count = 0;
    FILE *file = fopen(FS_MQTT_LOG_FILE, "r");
    if (file == NULL)
    {
        ESP_LOGI(TAG, "MQTT log file not found");
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    ESP_LOGI(TAG, "MQTT log file size: %ld bytes", file_size);

    if (file_size == 0)
    {
        ESP_LOGI(TAG, "MQTT log file is empty");
        fclose(file);
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    size_t free_heap_before_buffer = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Allocating buffer for file content: %ld bytes (free heap: %zu)", file_size + 1, free_heap_before_buffer);

    char *buffer = malloc(file_size + 1);
    if (buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer: %ld bytes (free heap: %zu)", file_size + 1, free_heap_before_buffer);
        fclose(file);
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_ERR_NO_MEM;
    }

    size_t bytes_read = fread(buffer, 1, file_size, file);
    fclose(file);

    if (bytes_read != (size_t)file_size)
    {
        ESP_LOGW(TAG, "Failed to read complete file: expected %ld bytes, read %zu bytes", file_size, bytes_read);
        free(buffer);
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_ERR_INVALID_RESPONSE;
    }

    buffer[file_size] = '\0';

    ESP_LOGI(TAG, "Parsing JSON from buffer (%ld bytes)", file_size);
    cJSON *log_array = cJSON_Parse(buffer);
    const char *error_ptr = cJSON_GetErrorPtr();

    if (log_array == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON from log file. Error at: %s", error_ptr ? error_ptr : "unknown");
        // Log first 200 chars of buffer for debugging
        size_t log_len = file_size > 200 ? 200 : file_size;
        char log_buf[201];
        memcpy(log_buf, buffer, log_len);
        log_buf[log_len] = '\0';
        ESP_LOGE(TAG, "File content (first %zu chars): %s", log_len, log_buf);
        ESP_LOGW(TAG, "Clearing corrupted log file and starting fresh");
        free(buffer);
        xSemaphoreGive(s_spiffs_mutex);
        // Clear the corrupted file (will acquire mutex internally)
        fs_utils_clear_mqtt_logs();
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (!cJSON_IsArray(log_array))
    {
        ESP_LOGE(TAG, "Parsed JSON is not an array");
        cJSON_Delete(log_array);
        free(buffer);
        ESP_LOGW(TAG, "Clearing corrupted log file and starting fresh");
        xSemaphoreGive(s_spiffs_mutex);
        // Clear the corrupted file (will acquire mutex internally)
        fs_utils_clear_mqtt_logs();
        return ESP_ERR_INVALID_RESPONSE;
    }

    free(buffer);
    size_t free_heap_after_buffer = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Buffer freed, free heap: %zu bytes", free_heap_after_buffer);

    int array_size = cJSON_GetArraySize(log_array);
    ESP_LOGI(TAG, "Found %d log entries in JSON array", array_size);

    if (array_size == 0)
    {
        ESP_LOGI(TAG, "No log entries found");
        cJSON_Delete(log_array);
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    // Use smaller fixed sizes - most topics are < 100 bytes, payloads < 200 bytes
    // This saves significant memory compared to 256/512 per entry
    #define TOPIC_SIZE 128
    #define PAYLOAD_SIZE 256

    // Calculate memory needed
    size_t topics_size = array_size * TOPIC_SIZE;
    size_t qos_size = array_size * sizeof(int);
    size_t payloads_size = array_size * PAYLOAD_SIZE;
    size_t timestamps_size = array_size * sizeof(time_t);
    size_t total_needed = topics_size + qos_size + payloads_size + timestamps_size;

    size_t free_heap_before_alloc = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Allocating arrays for %d entries:", array_size);
    ESP_LOGI(TAG, "  - topics: %zu bytes (%d * %d)", topics_size, array_size, TOPIC_SIZE);
    ESP_LOGI(TAG, "  - qos: %zu bytes (%d * %zu)", qos_size, array_size, sizeof(int));
    ESP_LOGI(TAG, "  - payloads: %zu bytes (%d * %d)", payloads_size, array_size, PAYLOAD_SIZE);
    ESP_LOGI(TAG, "  - timestamps: %zu bytes (%d * %zu)", timestamps_size, array_size, sizeof(time_t));
    ESP_LOGI(TAG, "  - Total needed: %zu bytes", total_needed);
    ESP_LOGI(TAG, "  - Free heap before allocation: %zu bytes", free_heap_before_alloc);

    // Allocate arrays (ID removed - not needed since logs are cleared on boot)
    *topics = malloc(topics_size);
    if (*topics == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate topics array: %zu bytes", topics_size);
    }
    else
    {
        ESP_LOGI(TAG, "Allocated topics array: %zu bytes", topics_size);
    }

    *qos = malloc(qos_size);
    if (*qos == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate qos array: %zu bytes", qos_size);
    }
    else
    {
        ESP_LOGI(TAG, "Allocated qos array: %zu bytes", qos_size);
    }

    *payloads = malloc(payloads_size);
    if (*payloads == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate payloads array: %zu bytes", payloads_size);
    }
    else
    {
        ESP_LOGI(TAG, "Allocated payloads array: %zu bytes", payloads_size);
    }

    *timestamps = malloc(timestamps_size);
    if (*timestamps == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate timestamps array: %zu bytes", timestamps_size);
    }
    else
    {
        ESP_LOGI(TAG, "Allocated timestamps array: %zu bytes", timestamps_size);
    }

    *log_ids = NULL; // Not used anymore

    if (*topics == NULL || *qos == NULL || *payloads == NULL || *timestamps == NULL)
    {
        ESP_LOGE(TAG, "One or more allocations failed - cleaning up");
        free(*topics);
        free(*qos);
        free(*payloads);
        free(*timestamps);
        cJSON_Delete(log_array);
        xSemaphoreGive(s_spiffs_mutex);
        size_t free_heap_after_fail = esp_get_free_heap_size();
        ESP_LOGE(TAG, "Free heap after cleanup: %zu bytes", free_heap_after_fail);
        return ESP_ERR_NO_MEM;
    }

    size_t free_heap_after_alloc = esp_get_free_heap_size();
    ESP_LOGI(TAG, "All arrays allocated successfully. Free heap after allocation: %zu bytes (used: %zu bytes)",
             free_heap_after_alloc, free_heap_before_alloc - free_heap_after_alloc);

    // Parse entries
    ESP_LOGI(TAG, "Parsing %d log entries", array_size);
    for (int i = 0; i < array_size; i++)
    {
        cJSON *entry = cJSON_GetArrayItem(log_array, i);
        if (entry == NULL)
        {
            ESP_LOGW(TAG, "Entry %d is NULL, skipping", i);
            continue;
        }

        cJSON *ts_item = cJSON_GetObjectItem(entry, "ts");
        cJSON *topic_item = cJSON_GetObjectItem(entry, "topic");
        cJSON *qos_item = cJSON_GetObjectItem(entry, "qos");
        cJSON *payload_item = cJSON_GetObjectItem(entry, "payload");
        // ID field removed - not needed since logs are cleared on boot

        if (topic_item != NULL && cJSON_IsString(topic_item))
        {
            strncpy(*topics + i * TOPIC_SIZE, topic_item->valuestring, TOPIC_SIZE - 1);
            (*topics)[i * TOPIC_SIZE + TOPIC_SIZE - 1] = '\0';
        }
        if (qos_item != NULL && cJSON_IsNumber(qos_item))
        {
            (*qos)[i] = qos_item->valueint;
        }
        if (ts_item != NULL && cJSON_IsNumber(ts_item))
        {
            (*timestamps)[i] = (time_t)ts_item->valuedouble;
        }
        if (payload_item != NULL)
        {
            // Payload is stored directly as JSON, just print it
            char *payload_str = cJSON_Print(payload_item);
            if (payload_str != NULL)
            {
                size_t payload_len = strlen(payload_str);
                if (payload_len >= PAYLOAD_SIZE)
                {
                    ESP_LOGW(TAG, "Entry %d payload too long (%zu bytes), truncating to %d", i, payload_len, PAYLOAD_SIZE - 1);
                }
                strncpy(*payloads + i * PAYLOAD_SIZE, payload_str, PAYLOAD_SIZE - 1);
                (*payloads)[i * PAYLOAD_SIZE + PAYLOAD_SIZE - 1] = '\0';
                free(payload_str);
            }
            else
            {
                ESP_LOGW(TAG, "Failed to print payload for entry %d", i);
            }
        }
    }

    *count = array_size;
    cJSON_Delete(log_array);
    xSemaphoreGive(s_spiffs_mutex);

    size_t free_heap_final = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Successfully loaded %zu MQTT log entries. Free heap: %zu bytes (started with %zu, used %zu)",
             *count, free_heap_final, free_heap_before, free_heap_before - free_heap_final);
    return ESP_OK;
}

esp_err_t fs_utils_remove_mqtt_log(const char *id)
{
    if (!fs_mounted || id == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_spiffs_mutex == NULL)
    {
        ESP_LOGE(TAG, "SPIFFS mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_spiffs_mutex, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to acquire SPIFFS mutex");
        return ESP_FAIL;
    }

    FILE *file = fopen(FS_MQTT_LOG_FILE, "r");
    if (file == NULL)
    {
        return ESP_ERR_NOT_FOUND;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size == 0)
    {
        fclose(file);
        return ESP_ERR_NOT_FOUND;
    }

    char *buffer = malloc(file_size + 1);
    if (buffer == NULL)
    {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    fread(buffer, 1, file_size, file);
    buffer[file_size] = '\0';
    fclose(file);

    cJSON *log_array = cJSON_Parse(buffer);
    free(buffer);

    if (log_array == NULL || !cJSON_IsArray(log_array))
    {
        if (log_array != NULL)
        {
            cJSON_Delete(log_array);
        }
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Find and remove entry with matching ID
    int array_size = cJSON_GetArraySize(log_array);
    bool found = false;
    for (int i = 0; i < array_size; i++)
    {
        cJSON *entry = cJSON_GetArrayItem(log_array, i);
        if (entry == NULL)
        {
            continue;
        }

        cJSON *id_item = cJSON_GetObjectItem(entry, "id");
        if (id_item != NULL && cJSON_IsString(id_item) && strcmp(id_item->valuestring, id) == 0)
        {
            cJSON_DeleteItemFromArray(log_array, i);
            found = true;
            ESP_LOGI(TAG, "Removed log entry with id: %s", id);
            break;
        }
    }

    if (!found)
    {
        ESP_LOGW(TAG, "Log entry with id '%s' not found in file (array_size=%d)", id, array_size);
    }

    // Save back to file
    file = fopen(FS_MQTT_LOG_FILE, "w");
    if (file == NULL)
    {
        cJSON_Delete(log_array);
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_FAIL;
    }

    char *json_string = cJSON_Print(log_array);
    if (json_string != NULL)
    {
        fprintf(file, "%s", json_string);
        free(json_string);
    }
    fclose(file);

    cJSON_Delete(log_array);
    xSemaphoreGive(s_spiffs_mutex);
    return ESP_OK;
}

esp_err_t fs_utils_clear_mqtt_logs(void)
{
    if (!fs_mounted)
    {
        return ESP_ERR_INVALID_STATE;
    }

    remove(FS_MQTT_LOG_FILE);
    return ESP_OK;
}

size_t fs_utils_get_mqtt_log_count(void)
{
    if (!fs_mounted)
    {
        return 0;
    }

    if (s_spiffs_mutex == NULL)
    {
        ESP_LOGE(TAG, "SPIFFS mutex not initialized");
        return 0;
    }

    if (xSemaphoreTake(s_spiffs_mutex, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to acquire SPIFFS mutex");
        return 0;
    }

    FILE *file = fopen(FS_MQTT_LOG_FILE, "r");
    if (file == NULL)
    {
        xSemaphoreGive(s_spiffs_mutex);
        return 0;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size == 0)
    {
        fclose(file);
        xSemaphoreGive(s_spiffs_mutex);
        return 0;
    }

    char *buffer = malloc(file_size + 1);
    if (buffer == NULL)
    {
        fclose(file);
        xSemaphoreGive(s_spiffs_mutex);
        return 0;
    }

    fread(buffer, 1, file_size, file);
    buffer[file_size] = '\0';
    fclose(file);

    cJSON *log_array = cJSON_Parse(buffer);
    free(buffer);

    if (log_array == NULL || !cJSON_IsArray(log_array))
    {
        if (log_array != NULL)
        {
            cJSON_Delete(log_array);
        }
        xSemaphoreGive(s_spiffs_mutex);
        return 0;
    }

    size_t count = cJSON_GetArraySize(log_array);
    cJSON_Delete(log_array);
    xSemaphoreGive(s_spiffs_mutex);
    return count;
}

// Provisioning file functions
esp_err_t fs_utils_save_root_ca(const char *root_ca_pem, size_t len)
{
    if (!fs_mounted)
    {
        ESP_LOGE(TAG, "Filesystem not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    FILE *file = fopen(FS_ROOT_CA_FILE, "w");
    if (file == NULL)
    {
        ESP_LOGE(TAG, "Failed to open root CA file for writing: %s", FS_ROOT_CA_FILE);
        return ESP_FAIL;
    }

    size_t written = fwrite(root_ca_pem, 1, len, file);
    fclose(file);

    if (written != len)
    {
        ESP_LOGE(TAG, "Failed to write all bytes: wrote %zu of %zu", written, len);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saved root CA to filesystem (%zu bytes) at %s", len, FS_ROOT_CA_FILE);
    return ESP_OK;
}

esp_err_t fs_utils_load_root_ca(char *root_ca_pem, size_t *len)
{
    if (!fs_mounted)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_spiffs_mutex == NULL)
    {
        ESP_LOGE(TAG, "SPIFFS mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_spiffs_mutex, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to acquire SPIFFS mutex");
        return ESP_FAIL;
    }

    FILE *file = fopen(FS_ROOT_CA_FILE, "r");
    if (file == NULL)
    {
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size > *len)
    {
        fclose(file);
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_ERR_NO_MEM;
    }

    size_t read = fread(root_ca_pem, 1, file_size, file);
    fclose(file);

    if (read != file_size)
    {
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_FAIL;
    }

    *len = read;
    xSemaphoreGive(s_spiffs_mutex);
    return ESP_OK;
}

esp_err_t fs_utils_save_device_certificate(const char *device_cert_pem, size_t len)
{
    if (!fs_mounted)
    {
        ESP_LOGE(TAG, "Filesystem not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    FILE *file = fopen(FS_DEVICE_CERT_FILE, "w");
    if (file == NULL)
    {
        ESP_LOGE(TAG, "Failed to open device cert file for writing: %s", FS_DEVICE_CERT_FILE);
        return ESP_FAIL;
    }

    size_t written = fwrite(device_cert_pem, 1, len, file);
    fclose(file);

    if (written != len)
    {
        ESP_LOGE(TAG, "Failed to write all bytes: wrote %zu of %zu", written, len);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saved device certificate to filesystem (%zu bytes) at %s", len, FS_DEVICE_CERT_FILE);
    return ESP_OK;
}

esp_err_t fs_utils_load_device_certificate(char *device_cert_pem, size_t *len)
{
    if (!fs_mounted)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_spiffs_mutex == NULL)
    {
        ESP_LOGE(TAG, "SPIFFS mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_spiffs_mutex, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to acquire SPIFFS mutex");
        return ESP_FAIL;
    }

    FILE *file = fopen(FS_DEVICE_CERT_FILE, "r");
    if (file == NULL)
    {
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size > *len)
    {
        fclose(file);
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_ERR_NO_MEM;
    }

    size_t read = fread(device_cert_pem, 1, file_size, file);
    fclose(file);

    if (read != file_size)
    {
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_FAIL;
    }

    *len = read;
    xSemaphoreGive(s_spiffs_mutex);
    return ESP_OK;
}

esp_err_t fs_utils_save_private_key(const char *private_key_pem, size_t len)
{
    if (!fs_mounted)
    {
        ESP_LOGE(TAG, "Filesystem not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    FILE *file = fopen(FS_PRIVATE_KEY_FILE, "w");
    if (file == NULL)
    {
        ESP_LOGE(TAG, "Failed to open private key file for writing: %s", FS_PRIVATE_KEY_FILE);
        return ESP_FAIL;
    }

    size_t written = fwrite(private_key_pem, 1, len, file);
    fclose(file);

    if (written != len)
    {
        ESP_LOGE(TAG, "Failed to write all bytes: wrote %zu of %zu", written, len);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saved private key to filesystem (%zu bytes) at %s", len, FS_PRIVATE_KEY_FILE);
    return ESP_OK;
}

esp_err_t fs_utils_load_private_key(char *private_key_pem, size_t *len)
{
    if (!fs_mounted)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_spiffs_mutex == NULL)
    {
        ESP_LOGE(TAG, "SPIFFS mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_spiffs_mutex, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to acquire SPIFFS mutex");
        return ESP_FAIL;
    }

    FILE *file = fopen(FS_PRIVATE_KEY_FILE, "r");
    if (file == NULL)
    {
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size > *len)
    {
        fclose(file);
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_ERR_NO_MEM;
    }

    size_t read = fread(private_key_pem, 1, file_size, file);
    fclose(file);

    if (read != file_size)
    {
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_FAIL;
    }

    *len = read;
    xSemaphoreGive(s_spiffs_mutex);
    return ESP_OK;
}

esp_err_t fs_utils_save_client_id(const char *client_id)
{
    if (!fs_mounted)
    {
        return ESP_ERR_INVALID_STATE;
    }

    FILE *file = fopen(FS_CLIENT_ID_FILE, "w");
    if (file == NULL)
    {
        ESP_LOGE(TAG, "Failed to open client ID file for writing");
        return ESP_FAIL;
    }

    fprintf(file, "%s", client_id);
    fclose(file);
    ESP_LOGI(TAG, "Saved client ID to filesystem: %s", client_id);
    return ESP_OK;
}

esp_err_t fs_utils_load_client_id(char *client_id, size_t client_id_len)
{
    if (!fs_mounted)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_spiffs_mutex == NULL)
    {
        ESP_LOGE(TAG, "SPIFFS mutex not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_spiffs_mutex, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to acquire SPIFFS mutex");
        return ESP_FAIL;
    }

    FILE *file = fopen(FS_CLIENT_ID_FILE, "r");
    if (file == NULL)
    {
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    if (fgets(client_id, client_id_len, file) == NULL)
    {
        fclose(file);
        xSemaphoreGive(s_spiffs_mutex);
        return ESP_FAIL;
    }

    // Remove newline if present
    size_t len = strlen(client_id);
    if (len > 0 && client_id[len - 1] == '\n')
    {
        client_id[len - 1] = '\0';
    }

    fclose(file);
    ESP_LOGI(TAG, "Loaded client ID from filesystem: %s", client_id);
    xSemaphoreGive(s_spiffs_mutex);
    return ESP_OK;
}