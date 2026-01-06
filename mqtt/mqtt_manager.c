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

#include "event_manager.h"
#include "mqtt_manager.h"
#include "utils/nvs_utils.h"

#define AWS_IOT_ENDPOINT "aqbxwrwwgdb49-ats.iot.eu-north-1.amazonaws.com"
#define BROKER_URL "mqtt://10.177.164.196:1883" // Fallback for development

static const char *TAG = "mqtt_manager";

static char user_id[18];
static char device_mac[18];
static char temperature_topic[128]; // topic_id (64) + / + device_mac (17) + /data/temperature (17) = 99 max
static char ph_topic[128];
static char feed_topic[128];
static char cmd_topic[128];
static char logs_topic[128];
static esp_mqtt_client_handle_t g_client = NULL;
static char temp_frequency = 0;
static char feed_frequency = 0;

static void process_command(const char *cmd, int len)
{
    char buf[32];
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    memcpy(buf, cmd, len);
    buf[len] = 0;

    int value;

    if (sscanf(buf, "set temp %d", &value) == 1)
    {
        if (value >= 0)
        {
            temp_frequency = value;
            event_manager_set_bits(EVENT_BIT_TEMP_RESCHEDULED);
        }
        else
        {
            ESP_LOGW(TAG, "Invalid temperature interval: %d (must be >= 0)", value);
        }
    }

    else if (sscanf(buf, "set feed %d", &value) == 1)
    {
        if (value >= 0)
        {
            feed_frequency = value;
            event_manager_set_bits(EVENT_BIT_FEED_RESCHEDULED);
        }
        else
        {
            ESP_LOGW(TAG, "Invalid feeding interval: %d (must be >= 0)", value);
        }
    }

    else if (strcmp(buf, "force temp") == 0)
    {
        event_manager_set_bits(EVENT_BIT_TEMP_SCHEDULED);
    }
    else if (strcmp(buf, "force feed") == 0)
    {
        event_manager_set_bits(EVENT_BIT_FEED_SCHEDULED);
    }
    else if (strcmp(buf, "force ph") == 0)
    {
        event_manager_set_bits(EVENT_BIT_PH_SCHEDULED);
    }
}

static esp_err_t get_mac_address_string(char *mac_str)
{
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get base MAC address");
        return ret;
    }

    snprintf(mac_str, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "Aquatest MAC: %s", mac_str);
    return ESP_OK;
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

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    g_client = event->client;

    switch (event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        event_manager_set_bits(EVENT_BIT_MQTT_STATUS);
        esp_mqtt_client_subscribe(g_client, cmd_topic, 1);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        break;

    case MQTT_EVENT_DATA:
        if (event->topic_len == strlen(cmd_topic) && strncmp(event->topic, cmd_topic, event->topic_len) == 0)
        {
            process_command(event->data, event->data_len);
        }
        break;
    }
}

esp_err_t mqtt_manager_start(void)
{
    return esp_mqtt_client_start(g_client);
}

void mqtt_manager_stop(void)
{
    esp_mqtt_client_stop(g_client);
}

void mqtt_manager_enqueue_data(const char *topic, const char *data)
{
    if (g_client == NULL)
    {
        ESP_LOGW(TAG, "MQTT client not initialized, cannot enqueue message (topic: %s)", topic);
        return;
    }

    const char *target_topic = NULL;
    if (strcmp(topic, "temperature") == 0)
        target_topic = temperature_topic;
    else if (strcmp(topic, "ph") == 0)
        target_topic = ph_topic;
    else if (strcmp(topic, "feed") == 0)
        target_topic = feed_topic;
    else if (strcmp(topic, "logs") == 0)
        target_topic = logs_topic;
    else
    {
        ESP_LOGW(TAG, "Unknown topic: %s", topic);
        return;
    }

    esp_err_t err = esp_mqtt_client_enqueue(g_client, target_topic, data, 0, 1, 0, true);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Enqueued message to %s (will be sent when connected)", target_topic);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to enqueue message to %s: %s (0x%x)", target_topic, esp_err_to_name(err), err);
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

esp_err_t mqtt_manager_load_config(void)
{
    static char root_ca_buffer[2048] = {0};
    static char device_cert_buffer[2048] = {0};
    static char private_key_buffer[2048] = {0};
    static char topic_id[64] = {0};
    static char broker_url[512];

    esp_err_t err;

    err = nvs_read_root_ca(root_ca_buffer, sizeof(root_ca_buffer));
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Root CA not found in NVS");
    }

    err = nvs_read_device_certificate(device_cert_buffer, sizeof(device_cert_buffer));
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Device certificate not found in NVS");
        return ESP_ERR_NOT_FOUND;
    }

    err = nvs_read_private_key(private_key_buffer, sizeof(private_key_buffer));
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Private key not found in NVS");
        return ESP_ERR_NOT_FOUND;
    }

    // Load topic_id
    err = nvs_read_topic_id(topic_id, sizeof(topic_id));
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Topic ID not found in NVS");
        return ESP_ERR_NOT_FOUND;
    }

    // Get device MAC
    if (get_mac_address_string(device_mac) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get MAC address");
        return ESP_FAIL;
    }

    // Update topic strings
    snprintf(temperature_topic, sizeof(temperature_topic), "%s/%s/data/temperature", topic_id, device_mac);
    snprintf(ph_topic, sizeof(ph_topic), "%s/%s/data/ph", topic_id, device_mac);
    snprintf(feed_topic, sizeof(feed_topic), "%s/%s/data/feed", topic_id, device_mac);
    snprintf(cmd_topic, sizeof(cmd_topic), "%s/%s/cmd", topic_id, device_mac);
    snprintf(logs_topic, sizeof(logs_topic), "%s/%s/logs", topic_id, device_mac);

    // Stop and destroy existing client if it exists
    if (g_client != NULL)
    {
        esp_mqtt_client_stop(g_client);
        esp_mqtt_client_destroy(g_client);
        g_client = NULL;
    }

    // Build broker URL using hardcoded endpoint
    snprintf(broker_url, sizeof(broker_url), "mqtts://%s:8883", AWS_IOT_ENDPOINT);

    // Initialize MQTT client with new certificates
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_url,
        .broker.verification.certificate = (strlen(root_ca_buffer) > 0) ? root_ca_buffer : NULL,
        .credentials.authentication.certificate = device_cert_buffer,
        .credentials.authentication.key = private_key_buffer,
        .credentials.client_id = device_mac,
    };

    g_client = esp_mqtt_client_init(&mqtt_cfg);
    if (g_client == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(g_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    ESP_LOGI(TAG, "MQTT configuration reloaded: endpoint=%s, topic_id=%s", AWS_IOT_ENDPOINT, topic_id);
    return ESP_OK;
}

void mqtt_manager_init(void)
{
    initialize_sntp();

    // Try to load configuration from NVS
    esp_err_t err = mqtt_manager_load_config();
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "MQTT client initialized with NVS configuration");
        // Don't auto-start, let event_manager handle connection
        return;
    }

    // Fallback: initialize with default configuration (for development)
    ESP_LOGW(TAG, "MQTT config not found in NVS, using default configuration");

    if (get_mac_address_string(device_mac) != ESP_OK)
    {
        ESP_LOGE(TAG, "MAC error");
        return;
    }

    strncpy(user_id, "default_user", sizeof(user_id) - 1);
    snprintf(temperature_topic, sizeof(temperature_topic), "%s/%s/data/temperature", user_id, device_mac);
    snprintf(ph_topic, sizeof(ph_topic), "%s/%s/data/ph", user_id, device_mac);
    snprintf(feed_topic, sizeof(feed_topic), "%s/%s/data/feed", user_id, device_mac);
    snprintf(cmd_topic, sizeof(cmd_topic), "%s/%s/cmd", user_id, device_mac);
    snprintf(logs_topic, sizeof(logs_topic), "%s/%s/logs", user_id, device_mac);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = BROKER_URL,
        .credentials.client_id = device_mac,
    };

    g_client = esp_mqtt_client_init(&mqtt_cfg);
    if (g_client == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return;
    }

    esp_mqtt_client_register_event(g_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    ESP_LOGI(TAG, "MQTT client initialized with default configuration");
}