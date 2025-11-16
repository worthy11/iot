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
#include "../wifi_manager.h"

#include "esp_log.h"
#include "mqtt_client.h"

#define BROKER_URL "mqtt://10.72.5.219:1883"

static const char *TAG = "MQTT_PUBLISHER";

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

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected, ready to publish from queue");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;
    default:
        break;
    }
}

static void mqtt_app_start(void)
{
    char device_mac[18];
    char topic[64];
    
    if (get_mac_address_string(device_mac) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get MAC address, aborting MQTT");
        return;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = BROKER_URL,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    snprintf(topic, sizeof(topic), "/test/%s", device_mac);

    int msg_id;
    msg_id = esp_mqtt_client_enqueue(client, topic, "Siema", 0, 1, 0, true);
    ESP_LOGI(TAG, "Enqueued msg_id=%d", msg_id);

    msg_id = esp_mqtt_client_enqueue(client, topic, "Siema ponownie", 0, 1, 0, true);
    ESP_LOGI(TAG, "Enqueued msg_id=%d", msg_id);

    esp_mqtt_client_start(client);
}

void init_mqtt(void)
{
    ESP_LOGI(TAG, "Startup");
    ESP_ERROR_CHECK(nvs_flash_init());
    init_wifi_manager();

    mqtt_app_start();
}
