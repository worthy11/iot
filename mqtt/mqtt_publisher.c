#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "../wifi_manager.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"

// #define BROKER_URL "mqtts://10.72.5.219:8883"
#define BROKER_URL "mqtt://10.72.5.43:1883"

static const char *TAG = "MQTT_PUBLISHER";

static int publish_interval_sec = 5;
static esp_mqtt_client_handle_t g_client = NULL;

static char device_mac[18];
static char data_topic[64];
static char cmd_topic[64];
char msg[16];
static bool publish_enabled = false;

static void publish_task(void *param)
{
    while (1) {
        if (g_client != NULL && publish_enabled) {
            int value = (int)(esp_random() % 101);
            snprintf(msg, sizeof(msg), "%d *C", value);
            int msg_id = esp_mqtt_client_enqueue(g_client, data_topic, msg, 0, 1, 0, true);
            ESP_LOGI(TAG, "Enqueue -> \"%s\" msg_id=%d", msg, msg_id);
        }
        vTaskDelay(publish_interval_sec * 1000 / portTICK_PERIOD_MS);
    }
}

static void process_command(const char *cmd, int len)
{
    char buf[32];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, cmd, len);
    buf[len] = 0;

    int value;
    if (sscanf(buf, "temperature %d", &value) == 1) {
        publish_interval_sec = value;
        ESP_LOGW(TAG, "Interval set to %d seconds", publish_interval_sec);
        if (!publish_enabled) {
            publish_enabled = true;
            ESP_LOGI(TAG, "Start publishing temperature data");
        }
    }
    else if (sscanf(buf, "stop") == 1) {
        publish_enabled = false;
        ESP_LOGI(TAG, "Publishing stopped");
    }
    else {
        ESP_LOGW(TAG, "Unknown command: %s", buf);
    }
}

static esp_err_t get_mac_address_string(char *mac_str)
{
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get base MAC address");
        return ret;
    }

    snprintf(mac_str, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    ESP_LOGI(TAG, "Aquatest MAC: %s", mac_str);
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    g_client = event->client;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        esp_mqtt_client_subscribe(g_client, cmd_topic, 1);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "CMD [%.*s] -> [%.*s]",
                 event->topic_len, event->topic,
                 event->data_len, event->data);
        process_command(event->data, event->data_len);
        break;

    default:
        break;
    }
}

static void mqtt_app_start(void)
{
    if (get_mac_address_string(device_mac) != ESP_OK) {
        ESP_LOGE(TAG, "MAC error");
        return;
    }

    snprintf(data_topic, sizeof(data_topic), "devices/%s/data/temperature", device_mac);
    snprintf(cmd_topic, sizeof(cmd_topic),  "devices/%s/cmd", device_mac);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = BROKER_URL,
    };

    g_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(g_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(g_client);

    xTaskCreate(publish_task, "publish_task", 4096, NULL, 5, NULL);
}

void init_mqtt(void)
{
    ESP_LOGI(TAG, "Startup");
    ESP_ERROR_CHECK(nvs_flash_init());
    init_wifi_manager();

    mqtt_app_start();
}
