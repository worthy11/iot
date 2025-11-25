#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "wifi_manager.h"
#include "hardware_manager.h"
#include "nvs.h"
#include "nvs_flash.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_STATUS_BIT BIT0

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

// #define TEMP_HARDCODED_SSID "worthy hotspot"
// #define TEMP_HARDCODED_PASS "worthy11"
#define TEMP_HARDCODED_SSID "67 41"
#define TEMP_HARDCODED_PASS "gowno1234"

static const char *TAG = "wifi_manager";
static EventGroupHandle_t s_wifi_event_group;
EventGroupHandle_t wifi_status_event_group = NULL;
static bool s_wifi_connected = false;

/* Simple NVS-based WiFi config storage */
#define WIFI_CONFIG_NAMESPACE "wifi_cfg"

typedef struct {
    char ssid[32];
    char password[64];
} app_wifi_config_t;

static esp_err_t wifi_config_load(app_wifi_config_t *out_cfg)
{
    if (!out_cfg) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    size_t ssid_len = sizeof(out_cfg->ssid);
    size_t pass_len = sizeof(out_cfg->password);

    err = nvs_get_str(handle, "ssid", out_cfg->ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_get_str(handle, "pass", out_cfg->password, &pass_len);
    nvs_close(handle);
    return err;
}

static bool wifi_config_is_valid(const app_wifi_config_t *cfg)
{
    if (!cfg) return false;
    if (cfg->ssid[0] == '\0') return false;
    if (strlen(cfg->password) < 8) return false;
    return true;
}

static esp_err_t wifi_config_save(const app_wifi_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_str(handle, "ssid", cfg->ssid);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_set_str(handle, "pass", cfg->password);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (wifi_status_event_group != NULL)
        {
            xEventGroupClearBits(wifi_status_event_group, WIFI_STATUS_BIT);
        }
        s_wifi_connected = false;
        ESP_LOGI(TAG, "WiFi disconnected. Retrying connection to the AP");
        if (led_task_handle != NULL)
        {
            xTaskNotify(led_task_handle, 1, eSetValueWithOverwrite);
        }
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi connected. Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (wifi_status_event_group != NULL)
        {
            xEventGroupSetBits(wifi_status_event_group, WIFI_STATUS_BIT);
        }
        if (led_task_handle != NULL)
        {
            xTaskNotify(led_task_handle, 1, eSetValueWithOverwrite);
        }
    }
}

void init_wifi_manager(void)
{
    s_wifi_event_group = xEventGroupCreate();
    wifi_status_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_wifi_init(&(wifi_init_config_t)WIFI_INIT_CONFIG_DEFAULT()));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    app_wifi_config_t stored_cfg = {0};
    bool use_stored = false;

    if (wifi_config_load(&stored_cfg) == ESP_OK && wifi_config_is_valid(&stored_cfg))
    {
        use_stored = true;
        ESP_LOGI(TAG, "Using WiFi config from NVS: ssid='%s'", stored_cfg.ssid);
    }
    else
    {
        ESP_LOGW(TAG, "No valid WiFi config in NVS, falling back to hardcoded credentials");
    }

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };

    const char *ssid = use_stored ? stored_cfg.ssid : TEMP_HARDCODED_SSID;
    const char *pass = use_stored ? stored_cfg.password : TEMP_HARDCODED_PASS;

    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi init finished. Connected to AP.");
}

bool wifi_manager_is_connected(void)
{
    return s_wifi_connected;
}

/* Public function to save WiFi credentials to NVS - for testing/BLE use */
esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password)
{
    if (!ssid || !password) {
        ESP_LOGE(TAG, "Invalid SSID or password");
        return ESP_ERR_INVALID_ARG;
    }

    app_wifi_config_t cfg = {0};
    strncpy(cfg.ssid, ssid, sizeof(cfg.ssid) - 1);
    strncpy(cfg.password, password, sizeof(cfg.password) - 1);

    if (!wifi_config_is_valid(&cfg)) {
        ESP_LOGE(TAG, "WiFi config validation failed (ssid='%s')", ssid);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = wifi_config_save(&cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials saved to NVS: ssid='%s'", ssid);
    } else {
        ESP_LOGE(TAG, "Failed to save WiFi credentials: %s", esp_err_to_name(err));
    }
    return err;
}