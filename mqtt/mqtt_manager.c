#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "esp_sntp.h"
#include "mqtt_client.h"
#include "nvs.h"

#include "event_manager.h"
#include "mqtt_manager.h"
#include "utils/fs_utils.h"
#include "utils/nvs_utils.h"
#include "cJSON.h"

#define AWS_IOT_ENDPOINT "aqbxwrwwgdb49-ats.iot.eu-north-1.amazonaws.com"
static const char *TAG = "mqtt_manager";

static char client_id[64] = {0};
static esp_mqtt_client_handle_t g_client = NULL;
static char temp_frequency = 0;
static char feed_frequency = 0;
static char wake_frequency = 0;

static char thing_name[64] = {0};
static char shadow_get_topic[256] = {0};
static char shadow_update_topic[256] = {0};

typedef struct
{
    int msg_id;
    char log_id[37];
    time_t timestamp;
} pending_message_t;

void mqtt_manager_enqueue_temperature(float temperature);
void mqtt_manager_enqueue_ph(float ph);
void mqtt_manager_enqueue_feed(bool success);
void mqtt_manager_enqueue_log(const char *event, const char *value);

static void add_timestamp_to_json(char *buffer, size_t buffer_size, const char *message)
{
    time_t timestamp = time(NULL);

    if (strstr(message, "\"timestamp\"") != NULL)
    {
        strncpy(buffer, message, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return;
    }

    const char *last_brace = strrchr(message, '}');
    if (last_brace == NULL)
    {
        strncpy(buffer, message, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return;
    }

    size_t prefix_len = last_brace - message;
    if (prefix_len >= buffer_size - 50)
    {
        strncpy(buffer, message, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return;
    }

    memcpy(buffer, message, prefix_len);
    buffer[prefix_len] = '\0';

    if (prefix_len > 1 && buffer[prefix_len - 1] != '{' && buffer[prefix_len - 1] != ',')
    {
        strcat(buffer, ",");
    }

    char timestamp_str[32];
    snprintf(timestamp_str, sizeof(timestamp_str), "\"timestamp\":%ld", (long)timestamp);
    strcat(buffer, timestamp_str);

    strcat(buffer, "}");
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

static void build_topics(void)
{
    if (client_id[0] == '\0')
    {
        ESP_LOGE(TAG, "Cannot build topics: client_id not set");
        return;
    }

    strncpy(thing_name, client_id, sizeof(thing_name) - 1);
    thing_name[sizeof(thing_name) - 1] = '\0';

    snprintf(shadow_get_topic, sizeof(shadow_get_topic),
             "$aws/things/%s/shadow/get", thing_name);
    snprintf(shadow_update_topic, sizeof(shadow_update_topic),
             "$aws/things/%s/shadow/update", thing_name);

    ESP_LOGI(TAG, "Topics built: thing_name=%s", thing_name);
}

static void build_shadow_topic(char *buf, size_t buf_size, const char *base_topic, const char *suffix)
{
    if (suffix != NULL && suffix[0] != '\0')
    {
        snprintf(buf, buf_size, "%s/%s", base_topic, suffix);
    }
    else
    {
        strncpy(buf, base_topic, buf_size - 1);
        buf[buf_size - 1] = '\0';
    }
}

static void publish(const char *topic, const char *message)
{
    static char target_topic[256];
    const char *final_message;
    bool is_shadow_topic = (strncmp(topic, "$aws/", 5) == 0);

    if (is_shadow_topic)
    {
        final_message = message;
        strncpy(target_topic, topic, sizeof(target_topic) - 1);
        target_topic[sizeof(target_topic) - 1] = '\0';
    }
    else
    {
        // Message already has timestamp from enqueue time
        final_message = message;
        snprintf(target_topic, sizeof(target_topic), "%s/%s", client_id, topic);
    }

    EventBits_t bits = event_manager_get_bits();
    bool is_connected = (bits & EVENT_BIT_MQTT_STATUS) && (bits & EVENT_BIT_WIFI_STATUS);
    if (!is_connected)
    {
        ESP_LOGI(TAG, "Not connected, cannot publish");
        return;
    }

    int msg_id = esp_mqtt_client_publish(g_client, target_topic, final_message, 0, 1, 0);
    if (msg_id < 0)
    {
        ESP_LOGE(TAG, "Failed to publish message");
    }

    ESP_LOGI(TAG, "Published message to topic %s", target_topic);
}

static void publish_queued(void)
{
    char *topics = NULL;
    int *qos = NULL;
    char *payloads = NULL;
    time_t *timestamps = NULL;
    char **log_ids = NULL;
    size_t count = 0;

    // Small delay to ensure filesystem has synced any recent writes
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_err_t err = fs_utils_load_mqtt_logs(&topics, &qos, &payloads, &timestamps, &log_ids, &count);
    if (err != ESP_OK || count == 0)
    {
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to load queued messages from filesystem: %s", esp_err_to_name(err));
        }
        else
        {
            ESP_LOGI(TAG, "No queued messages to publish from filesystem");
        }
        return;
    }

    ESP_LOGI(TAG, "Publishing %zu queued messages from filesystem", count);

    for (size_t i = 0; i < count; i++)
    {
        char *full_topic = topics + i * 256;
        char *topic_suffix = strchr(full_topic, '/');
        if (topic_suffix == NULL)
        {
            ESP_LOGW(TAG, "Topic has no '/', using as-is: %s", full_topic);
            topic_suffix = full_topic;
        }
        else
        {
            topic_suffix++;
        }

        // Use the publish function
        publish(topic_suffix, payloads + i * 512);
    }

    // Clear the entire log file after publishing all queued messages
    fs_utils_clear_mqtt_logs();
    ESP_LOGI(TAG, "Cleared all queued messages from log after publishing");

    if (log_ids != NULL)
    {
        for (size_t i = 0; i < count; i++)
        {
            if (log_ids[i] != NULL)
                free(log_ids[i]);
        }
        free(log_ids);
    }
    if (topics != NULL)
        free(topics);
    if (qos != NULL)
        free(qos);
    if (payloads != NULL)
        free(payloads);
    if (timestamps != NULL)
        free(timestamps);
}

static void publish_shadow_update(cJSON *commands)
{
    if (shadow_update_topic[0] == '\0' || g_client == NULL)
    {
        ESP_LOGW(TAG, "Cannot publish shadow update: shadow_update_topic not built or client not set");
        return;
    }

    // Create shadow update JSON
    cJSON *update_json = cJSON_CreateObject();
    cJSON *state_obj = cJSON_CreateObject();
    cJSON *reported_obj = cJSON_CreateObject();
    cJSON *desired_obj = cJSON_CreateObject();
    cJSON *desired_commands_obj = cJSON_CreateObject();

    // Iterate through each command in commands
    cJSON *command_item = commands->child;
    while (command_item != NULL)
    {
        if (cJSON_IsObject(command_item))
        {
            // Extract values from command and add to reported
            cJSON *field = command_item->child;
            while (field != NULL)
            {
                if (cJSON_IsString(field))
                {
                    cJSON_AddStringToObject(reported_obj, field->string, field->valuestring);
                }
                else if (cJSON_IsNumber(field))
                {
                    cJSON_AddNumberToObject(reported_obj, field->string, field->valuedouble);
                }
                else if (cJSON_IsBool(field))
                {
                    cJSON_AddBoolToObject(reported_obj, field->string, cJSON_IsTrue(field));
                }
                else if (cJSON_IsNull(field))
                {
                    cJSON_AddNullToObject(reported_obj, field->string);
                }

                field = field->next;
            }

            // Set the command key to null in desired.commands
            cJSON_AddNullToObject(desired_commands_obj, command_item->string);
        }

        command_item = command_item->next;
    }

    // Add commands to desired
    cJSON_AddItemToObject(desired_obj, "commands", desired_commands_obj);

    cJSON_AddItemToObject(state_obj, "reported", reported_obj);
    cJSON_AddItemToObject(state_obj, "desired", desired_obj);
    cJSON_AddItemToObject(update_json, "state", state_obj);

    // Convert to string
    char *json_string = cJSON_Print(update_json);
    if (json_string == NULL)
    {
        ESP_LOGE(TAG, "Failed to create shadow update JSON string");
        cJSON_Delete(update_json);
        return;
    }

    publish(shadow_update_topic, json_string);
    free(json_string);
    cJSON_Delete(update_json);
}

static void process_shadow_delta(const char *json_data, int len)
{
    char buf[1024];
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    memcpy(buf, json_data, len);
    buf[len] = 0;

    cJSON *json = cJSON_Parse(buf);
    if (json == NULL)
    {
        ESP_LOGW(TAG, "Failed to parse shadow delta JSON");
        return;
    }

    cJSON *state = cJSON_GetObjectItem(json, "state");
    if (state == NULL)
    {
        ESP_LOGW(TAG, "No 'state' object in shadow delta");
        cJSON_Delete(json);
        return;
    }

    // Check for commands structure: state.commands
    cJSON *commands = cJSON_GetObjectItem(state, "commands");
    if (commands == NULL || !cJSON_IsObject(commands))
    {
        ESP_LOGW(TAG, "No 'commands' object in shadow delta state");
        cJSON_Delete(json);
        return;
    }

    bool state_updated = false;

    // Iterate through each command in commands
    cJSON *command_item = commands->child;
    while (command_item != NULL)
    {
        if (cJSON_IsObject(command_item))
        {
            // Process each field in the command
            cJSON *field = command_item->child;
            while (field != NULL)
            {
                // Parse temp_frequency
                if (strcmp(field->string, "temp_frequency") == 0 && cJSON_IsNumber(field))
                {
                    int value = field->valueint;
                    if (value >= 0)
                    {
                        temp_frequency = value;
                        event_manager_set_temp_reading_interval(value);
                        ESP_LOGI(TAG, "Shadow delta: temp_frequency = %d", value);
                        state_updated = true;
                    }
                    else
                    {
                        ESP_LOGW(TAG, "Invalid temperature interval: %d (must be >= 0)", value);
                    }
                }
                // Parse feed_frequency
                else if (strcmp(field->string, "feed_frequency") == 0 && cJSON_IsNumber(field))
                {
                    int value = field->valueint;
                    if (value >= 0)
                    {
                        feed_frequency = value;
                        event_manager_set_feeding_interval(value);
                        ESP_LOGI(TAG, "Shadow delta: feed_frequency = %d", value);
                        state_updated = true;
                    }
                    else
                    {
                        ESP_LOGW(TAG, "Invalid feeding interval: %d (must be >= 0)", value);
                    }
                }
                // Parse wake_frequency
                else if (strcmp(field->string, "wake_frequency") == 0 && cJSON_IsNumber(field))
                {
                    int value = field->valueint;
                    if (value >= 0)
                    {
                        wake_frequency = value;
                        event_manager_set_publish_interval(value);
                        ESP_LOGI(TAG, "Shadow delta: wake_frequency = %d", value);
                        state_updated = true;
                    }
                    else
                    {
                        ESP_LOGW(TAG, "Invalid wake interval: %d (must be >= 0)", value);
                    }
                }

                else if (strcmp(field->string, "force_temp") == 0 && cJSON_IsTrue(field))
                {
                    event_manager_set_bits(EVENT_BIT_TEMP_SCHEDULED);
                    ESP_LOGI(TAG, "Shadow delta: force_temp = true");
                    state_updated = true;
                }
                else if (strcmp(field->string, "force_feed") == 0 && cJSON_IsTrue(field))
                {
                    event_manager_set_bits(EVENT_BIT_FEED_SCHEDULED);
                    ESP_LOGI(TAG, "Shadow delta: force_feed = true");
                    state_updated = true;
                }
                else if (strcmp(field->string, "force_ph") == 0 && cJSON_IsTrue(field))
                {
                    event_manager_set_bits(EVENT_BIT_PH_SCHEDULED);
                    ESP_LOGI(TAG, "Shadow delta: force_ph = true");
                    state_updated = true;
                }

                field = field->next;
            }
        }

        command_item = command_item->next;
    }

    if (state_updated)
    {
        publish_shadow_update(commands);
    }

    cJSON_Delete(json);
}

static void process_shadow_accepted(const char *json_data, int len)
{
    char buf[512];
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    memcpy(buf, json_data, len);
    buf[len] = 0;

    cJSON *json = cJSON_Parse(buf);
    if (json != NULL)
    {
        cJSON *state = cJSON_GetObjectItem(json, "state");
        if (state != NULL)
        {
            cJSON *desired = cJSON_GetObjectItem(state, "desired");
            if (desired != NULL)
            {
                ESP_LOGI(TAG, "Shadow accepted desired state");
            }
        }
        cJSON_Delete(json);
    }
}

static void process_get_accepted(const char *json_data, int len)
{
    char buf[1024];
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    memcpy(buf, json_data, len);
    buf[len] = 0;

    // Parse shadow state
    cJSON *json = cJSON_Parse(buf);
    if (json != NULL)
    {
        cJSON *state = cJSON_GetObjectItem(json, "state");
        if (state != NULL)
        {
            cJSON *desired = cJSON_GetObjectItem(state, "desired");
            if (desired != NULL && !cJSON_IsNull(desired))
            {
                // Create a wrapper JSON with "state" containing the desired values
                // This matches the format expected by process_shadow_delta
                cJSON *wrapper = cJSON_CreateObject();
                cJSON *state_wrapper = cJSON_CreateObject();

                // Copy all items from desired to state_wrapper
                cJSON *item = desired->child;
                while (item != NULL)
                {
                    cJSON *copy = cJSON_Duplicate(item, 1);
                    cJSON_AddItemToObject(state_wrapper, item->string, copy);
                    item = item->next;
                }

                cJSON_AddItemToObject(wrapper, "state", state_wrapper);

                // Convert to string and process
                char *json_string = cJSON_Print(wrapper);
                if (json_string != NULL)
                {
                    process_shadow_delta(json_string, strlen(json_string));
                    free(json_string);
                }

                cJSON_Delete(wrapper);
            }
        }
        cJSON_Delete(json);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to parse shadow/get/accepted JSON");
    }
}

void mqtt_manager_start(void)
{
    if (g_client == NULL)
    {
        ESP_LOGE(TAG, "MQTT client not initialized");
    }

    ESP_LOGI(TAG, "Starting MQTT client");
    esp_mqtt_client_start(g_client);
}

void mqtt_manager_stop(void)
{
    if (g_client != NULL)
    {
        if (shadow_update_topic[0] != '\0')
        {
            static char topic_buf[256];
            build_shadow_topic(topic_buf, sizeof(topic_buf), shadow_update_topic, "delta");
            esp_mqtt_client_unsubscribe(g_client, topic_buf);
            build_shadow_topic(topic_buf, sizeof(topic_buf), shadow_update_topic, "accepted");
            esp_mqtt_client_unsubscribe(g_client, topic_buf);
        }
        if (shadow_get_topic[0] != '\0')
        {
            static char topic_buf[256];
            build_shadow_topic(topic_buf, sizeof(topic_buf), shadow_get_topic, "accepted");
            esp_mqtt_client_unsubscribe(g_client, topic_buf);
        }
        esp_mqtt_client_disconnect(g_client);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_mqtt_client_stop(g_client);
    }
}

int mqtt_manager_get_temp_frequency(void)
{
    return temp_frequency;
}

int mqtt_manager_get_feed_frequency(void)
{
    return feed_frequency;
}

static void enqueue_message(const char *topic_suffix, const char *message)
{
    char message_with_timestamp[512];
    add_timestamp_to_json(message_with_timestamp, sizeof(message_with_timestamp), message);

    char target_topic[256];
    snprintf(target_topic, sizeof(target_topic), "%s/%s", client_id, topic_suffix);

    time_t timestamp = time(NULL);
    char log_id[37];
    esp_err_t err = fs_utils_save_mqtt_log(target_topic, 1, message_with_timestamp, timestamp, log_id, sizeof(log_id));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to enqueue message to topic %s: %s", topic_suffix, esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(TAG, "Message enqueued to topic %s", topic_suffix);
    }
}

void mqtt_manager_enqueue_temperature(float temperature)
{
    char message[128];
    snprintf(message, sizeof(message), "{\"temperature\": %f}", temperature);
    enqueue_message("temp", message);
}

void mqtt_manager_enqueue_ph(float ph)
{
    char message[128];
    snprintf(message, sizeof(message), "{\"ph\": %f}", ph);
    enqueue_message("ph", message);
}

void mqtt_manager_enqueue_feed(bool success)
{
    char message[128];
    snprintf(message, sizeof(message), "{\"feed\": %s}", success ? "true" : "false");
    enqueue_message("feed", message);
}

void mqtt_manager_enqueue_log(const char *event, const char *value)
{
    char message[128];
    snprintf(message, sizeof(message), "{\"event\": \"%s\", \"value\": \"%s\"}", event, value);
    enqueue_message("log", message);
}

void mqtt_manager_publish(void)
{
    publish_queued();
}

static void event_handler(void *handler_args,
                          esp_event_base_t base,
                          int32_t event_id,
                          void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    g_client = event->client;

    switch (event_id)
    {
    case MQTT_EVENT_CONNECTED:
    {
        ESP_LOGI(TAG, "MQTT connected");
        event_manager_set_bits(EVENT_BIT_MQTT_STATUS);

        if (shadow_update_topic[0] != '\0' && shadow_get_topic[0] != '\0')
        {
            static char topic_buf[256];

            // Subscribe to shadow/update/delta
            build_shadow_topic(topic_buf, sizeof(topic_buf), shadow_update_topic, "delta");
            esp_mqtt_client_subscribe(g_client, topic_buf, 1);

            // Subscribe to shadow/update/accepted
            build_shadow_topic(topic_buf, sizeof(topic_buf), shadow_update_topic, "accepted");
            esp_mqtt_client_subscribe(g_client, topic_buf, 1);

            // Subscribe to shadow/get/accepted
            build_shadow_topic(topic_buf, sizeof(topic_buf), shadow_get_topic, "accepted");
            esp_mqtt_client_subscribe(g_client, topic_buf, 1);

            // Publish empty payload to shadow/get to request current shadow state
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGI(TAG, "Requesting shadow state via shadow/get");
            esp_mqtt_client_publish(g_client, shadow_get_topic, "", 0, 1, 0);
        }
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        event_manager_clear_bits(EVENT_BIT_MQTT_STATUS);
        break;

    case MQTT_EVENT_DATA:
    {
        // Determine which topic the message came from
        if (event->topic_len > 0 && event->topic != NULL)
        {
            char topic_buf[256];
            int topic_len = event->topic_len < sizeof(topic_buf) - 1 ? event->topic_len : sizeof(topic_buf) - 1;
            memcpy(topic_buf, event->topic, topic_len);
            topic_buf[topic_len] = '\0';

            // Check if it's a delta topic
            if (strstr(topic_buf, "/shadow/update/delta") != NULL)
            {
                process_shadow_delta(event->data, event->data_len);
            }
            // Check if it's an accepted topic
            else if (strstr(topic_buf, "/shadow/update/accepted") != NULL)
            {
                process_shadow_accepted(event->data, event->data_len);
            }
            // Check if it's a get/accepted topic
            else if (strstr(topic_buf, "/shadow/get/accepted") != NULL)
            {
                process_get_accepted(event->data, event->data_len);
            }
            else
            {
                ESP_LOGW(TAG, "Received message on unknown topic: %s", topic_buf);
            }
        }
        else
        {
            process_shadow_delta(event->data, event->data_len);
        }
        break;
    }

    case MQTT_EVENT_PUBLISHED:
    {
        ESP_LOGI(TAG, "Message published: msg_id=%d", event->msg_id);
        break;
    }

    case MQTT_EVENT_ERROR:
        if (event->error_handle != NULL)
        {
            ESP_LOGE(TAG, "MQTT error: type=%d, esp_tls_last_esp_err=0x%x, esp_tls_stack_err=0x%x, esp_transport_sock_errno=%d",
                     event->error_handle->error_type,
                     event->error_handle->esp_tls_last_esp_err,
                     event->error_handle->esp_tls_stack_err,
                     event->error_handle->esp_transport_sock_errno);
        }
        else
        {
            ESP_LOGE(TAG, "MQTT error: error_handle is NULL");
        }
        event_manager_clear_bits(EVENT_BIT_MQTT_STATUS);
        break;
    }
}

esp_err_t mqtt_manager_load_config(void)
{
    static char root_ca_buffer[2048] = {0};
    static char device_cert_buffer[2048] = {0};
    static char private_key_buffer[2048] = {0};
    static char broker_url[512];

    esp_err_t err;

    size_t root_ca_len = sizeof(root_ca_buffer);
    err = fs_utils_load_root_ca(root_ca_buffer, &root_ca_len);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Root CA not found in filesystem");
    }

    size_t device_cert_len = sizeof(device_cert_buffer);
    err = fs_utils_load_device_certificate(device_cert_buffer, &device_cert_len);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Device certificate not found in filesystem");
        return ESP_ERR_NOT_FOUND;
    }

    size_t private_key_len = sizeof(private_key_buffer);
    err = fs_utils_load_private_key(private_key_buffer, &private_key_len);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Private key not found in filesystem");
        return ESP_ERR_NOT_FOUND;
    }

    err = fs_utils_load_client_id(client_id, sizeof(client_id));
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Client ID not found in filesystem");
        return ESP_ERR_NOT_FOUND;
    }

    build_topics();

    if (g_client != NULL)
    {
        esp_mqtt_client_disconnect(g_client);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_mqtt_client_stop(g_client);
        esp_mqtt_client_destroy(g_client);
        g_client = NULL;
    }

    snprintf(broker_url, sizeof(broker_url), "mqtts://%s:8883", AWS_IOT_ENDPOINT);

    esp_mqtt_client_config_t mqtt_cfg = {0};
    mqtt_cfg.broker.address.uri = broker_url;
    mqtt_cfg.credentials.authentication.certificate = device_cert_buffer;
    mqtt_cfg.credentials.authentication.key = private_key_buffer;
    mqtt_cfg.credentials.client_id = client_id;

    if (strlen(root_ca_buffer) > 0)
    {
        mqtt_cfg.broker.verification.certificate = root_ca_buffer;
        mqtt_cfg.broker.verification.skip_cert_common_name_check = false;
    }
    else
    {
        ESP_LOGE(TAG, "Root CA not found in NVS - cannot connect to AWS IoT without it");
        return ESP_ERR_NOT_FOUND;
    }

    g_client = esp_mqtt_client_init(&mqtt_cfg);
    if (g_client == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(g_client, ESP_EVENT_ANY_ID, event_handler, NULL);

    ESP_LOGI(TAG, "MQTT configuration reloaded: endpoint=%s, client_id=%s", AWS_IOT_ENDPOINT, client_id);
    return ESP_OK;
}

void mqtt_manager_init(void)
{
    initialize_sntp();

    // Clear all enqueued messages on startup
    esp_err_t clear_err = fs_utils_clear_mqtt_logs();
    if (clear_err == ESP_OK)
    {
        ESP_LOGI(TAG, "Cleared all enqueued messages on startup");
    }
    else if (clear_err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG, "Failed to clear enqueued messages on startup: %s", esp_err_to_name(clear_err));
    }

    esp_err_t err = mqtt_manager_load_config();
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "MQTT client initialized with NVS configuration");
        return;
    }

    ESP_LOGW(TAG, "MQTT config not found in NVS, using default configuration");

    strncpy(client_id, "default_user", sizeof(client_id) - 1);
    static char default_broker_url[128];
    snprintf(default_broker_url, sizeof(default_broker_url), "mqtts://%s:8883", AWS_IOT_ENDPOINT);
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = default_broker_url,
        .credentials.client_id = "default_client",
    };

    g_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(g_client, ESP_EVENT_ANY_ID, event_handler, NULL);
    ESP_LOGI(TAG, "MQTT client initialized with default configuration");
}