#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "event_manager.h"
#include "aquarium_data.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "esp_sntp.h"

// #define BROKER_URL "mqtt://10.72.5.219:1883"
// #define BROKER_URL "mqtt://10.88.236.219:1883"
#define BROKER_URL "mqtt://10.177.164.196:1883"

static const char *TAG = "MQTT_PUBLISHER";
static const char *user_id = "f8e87394";

static esp_mqtt_client_handle_t g_client = NULL;

static char device_mac[18];
static char temperature_topic[64];
static char ph_topic[64];
static char feed_topic[64];
static char cmd_topic[64];

static void temperature_publish_task(void *param)
{
    float last_value = -999;
    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(EVENT_BIT_TEMP_UPDATED,
                                                   true,
                                                   false,
                                                   portMAX_DELAY);

        if (g_client != NULL)
        {
            aquarium_data_t data;
            aquarium_data_get(&data);
            if (data.temp_reading_interval_sec > 0)
            {
                float value = data.temperature;
                if (value != last_value)
                {
                    time_t now = time(NULL);
                    char msg[64];
                    last_value = value;
                    snprintf(msg, sizeof(msg), "%.2f,%lld", value, (long long)now);
                    int msg_id = esp_mqtt_client_enqueue(g_client, temperature_topic, msg, 0, 1, 0, true);
                    if (msg_id >= 0)
                    {
                        ESP_LOGI(TAG, "Temp changed, enqueued -> \"%s\" msg_id=%d", msg, msg_id);
                    }
                    else
                    {
                        ESP_LOGW(TAG, "Failed to enqueue temperature message");
                    }
                }
            }
        }
    }
}

static void ph_publish_task(void *param)
{
    float last_value = -999.0f;

    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(EVENT_BIT_PH_UPDATED,
                                                   true,
                                                   false,
                                                   portMAX_DELAY);

        if (g_client != NULL)
        {
            aquarium_data_t data;
            aquarium_data_get(&data);
            float value = data.ph;

            if (value != last_value)
            {
                time_t now = time(NULL);
                char msg[64];
                last_value = value;
                snprintf(msg, sizeof(msg), "%.2f,%lld", value, (long long)now);
                int msg_id = esp_mqtt_client_enqueue(g_client, ph_topic, msg, 0, 1, 0, true);
                if (msg_id >= 0)
                {
                    ESP_LOGI(TAG, "pH changed, enqueued -> \"%s\" msg_id=%d", msg, msg_id);
                }
                else
                {
                    ESP_LOGW(TAG, "Failed to enqueue pH message");
                }
            }
        }
    }
}

static void feed_task(void *param)
{
    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(EVENT_BIT_FEED_UPDATED,
                                                   true,
                                                   false,
                                                   portMAX_DELAY);

        if (g_client != NULL)
        {
            aquarium_data_t data;
            aquarium_data_get(&data);
            if (data.feeding_interval_sec > 0)
            {
                char msg[64];
                struct tm *timeinfo = localtime(&data.last_feed_time);
                const char *status = data.last_feed_success ? "success" : "failure";
                snprintf(msg, sizeof(msg), "%lld,%02d:%02d:%02d,%s",
                         (long long)data.last_feed_time,
                         timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec,
                         status);
                int msg_id = esp_mqtt_client_enqueue(g_client, feed_topic, msg, 0, 1, 0, true);
                if (msg_id >= 0)
                {
                    ESP_LOGI(TAG, "Feed enqueued -> \"%s\" msg_id=%d", msg, msg_id);
                }
                else
                {
                    ESP_LOGW(TAG, "Failed to enqueue feed message");
                }
            }
        }
    }
}

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
            aquarium_data_set_temp_reading_interval((uint32_t)value);
            event_manager_set_bits(EVENT_BIT_TEMP_INTERVAL_CHANGED);
            if (value > 0)
            {
                ESP_LOGI(TAG, "Temperature reading interval set to %d seconds", value);
            }
            else
            {
                ESP_LOGI(TAG, "Temperature reading disabled");
            }
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
            aquarium_data_set_feeding_interval((uint32_t)value);
            event_manager_set_bits(EVENT_BIT_FEED_INTERVAL_CHANGED);
            if (value > 0)
            {
                ESP_LOGI(TAG, "Feeding interval set to %d seconds", value);
            }
            else
            {
                ESP_LOGI(TAG, "Feeding disabled");
            }
        }
        else
        {
            ESP_LOGW(TAG, "Invalid feeding interval: %d (must be >= 0)", value);
        }
    }
    // force temp - trigger immediate temperature measurement
    else if (strcmp(buf, "force temp") == 0)
    {
        ESP_LOGI(TAG, "Force temperature measurement requested");
        event_manager_set_bits(EVENT_BIT_MEASURE_TEMP);
    }
    // force feed - trigger immediate feed
    else if (strcmp(buf, "force feed") == 0)
    {
        ESP_LOGI(TAG, "Force feed requested");
        event_manager_set_bits(EVENT_BIT_FEED_SCHEDULED);
    }
    // force ph - trigger immediate pH measurement
    else if (strcmp(buf, "force ph") == 0)
    {
        ESP_LOGI(TAG, "Force pH measurement requested");
        event_manager_set_bits(EVENT_BIT_MEASURE_PH);
    }
    else
    {
        ESP_LOGW(TAG, "Unknown command: %s", buf);
    }
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
        esp_mqtt_client_subscribe(g_client, cmd_topic, 1);
        break;

    case MQTT_EVENT_DATA:
        if (event->topic_len == strlen(cmd_topic) &&
            strncmp(event->topic, cmd_topic, event->topic_len) == 0)
        {
            ESP_LOGI(TAG, "CMD -> [%.*s]",
                     event->data_len,
                     event->data);

            process_command(event->data, event->data_len);
        }
        break;

    default:
        break;
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

static void mqtt_app_start(void)
{
    if (get_mac_address_string(device_mac) != ESP_OK)
    {
        ESP_LOGE(TAG, "MAC error");
        return;
    }

    snprintf(temperature_topic, sizeof(temperature_topic), "%s/%s/data/temperature", user_id, device_mac);
    snprintf(ph_topic, sizeof(ph_topic), "%s/%s/data/ph", user_id, device_mac);
    snprintf(feed_topic, sizeof(feed_topic), "%s/%s/data/feed", user_id, device_mac);
    snprintf(cmd_topic, sizeof(cmd_topic), "%s/%s/cmd", user_id, device_mac);

    initialize_sntp();

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = BROKER_URL,
        .credentials.username = user_id};

    g_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(g_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(g_client);

    xTaskCreate(temperature_publish_task, "temperature_task", 4096, NULL, 5, NULL);
    xTaskCreate(ph_publish_task, "ph_task", 4096, NULL, 5, NULL);
    xTaskCreate(feed_task, "feed_task", 4096, NULL, 5, NULL);
}

void init_mqtt(void)
{
    mqtt_app_start();
}