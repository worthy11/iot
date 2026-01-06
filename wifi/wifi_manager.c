#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "wifi_manager.h"
#include "event_manager.h"
#include "utils/nvs_utils.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_manager";

typedef struct
{
    char ssid[32];
    char password[64];
} app_wifi_config_t;

#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#define WIFI_CONFIG_NAMESPACE "wifi_cfg"

static app_wifi_config_t current_cfg = {0};

static esp_err_t wifi_config_load(app_wifi_config_t *out_cfg)
{
    if (!out_cfg)
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    size_t ssid_len = sizeof(out_cfg->ssid);
    size_t pass_len = sizeof(out_cfg->password);

    err = nvs_get_str(handle, "ssid", out_cfg->ssid, &ssid_len);
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return err;
    }

    err = nvs_get_str(handle, "pass", out_cfg->password, &pass_len);
    nvs_close(handle);
    return err;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
        ESP_LOGI(TAG, "WiFi started. Connecting...");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Disconnected from the AP. Retrying...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected to the AP, Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        event_manager_set_bits(EVENT_BIT_WIFI_STATUS);
    }
}

esp_err_t wifi_manager_start(void)
{
    return esp_wifi_start();
}

void wifi_manager_stop(void)
{
    esp_wifi_stop();
}

const char *wifi_manager_get_current_ssid(void)
{
    return current_cfg.ssid;
}

const char *wifi_manager_get_current_password(void)
{
    return current_cfg.password;
}

esp_err_t wifi_manager_load_config(void)
{
    app_wifi_config_t cfg = {0};
    esp_err_t err = nvs_read_wifi_credentials(cfg.ssid, sizeof(cfg.ssid), cfg.password, sizeof(cfg.password));

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to load WiFi credentials from NVS: %s", esp_err_to_name(err));
        return err;
    }

    current_cfg = cfg;

    // Update WiFi configuration
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };

    strncpy((char *)wifi_config.sta.ssid, current_cfg.ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, current_cfg.password, sizeof(wifi_config.sta.password) - 1);

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "WiFi configuration reloaded: ssid='%s'", current_cfg.ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_clear_credentials(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS namespace for clearing credentials: %d", err);
        return err;
    }

    err = nvs_erase_key(handle, "ssid");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Failed to erase SSID key: %d", err);
    }

    err = nvs_erase_key(handle, "pass");
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "Failed to erase password key: %d", err);
    }

    err = nvs_commit(handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to commit NVS changes: %d", err);
        nvs_close(handle);
        return err;
    }

    nvs_close(handle);

    memset(&current_cfg, 0, sizeof(current_cfg));
    ESP_LOGI(TAG, "WiFi credentials cleared successfully");
    return ESP_OK;
}

esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password)
{
    if (strlen(ssid) == 0 && strlen(password) == 0)
    {
        return wifi_manager_clear_credentials();
    }

    app_wifi_config_t cfg = {0};
    strncpy(cfg.ssid, ssid, sizeof(cfg.ssid) - 1);
    strncpy(cfg.password, password, sizeof(cfg.password) - 1);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;

    // save ssid
    err = nvs_set_str(handle, "ssid", cfg.ssid);
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return err;
    }
    else if (strlen(ssid) > 0)
    {
        ESP_LOGI(TAG, "SSID saved to NVS: ssid='%s'", ssid);
    }
    else if (strlen(ssid) == 0)
    {
        ESP_LOGI(TAG, "SSID cleared from NVS");
    }

    err = nvs_set_str(handle, "pass", cfg.password);
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return err;
    }
    else if (strlen(password) > 0)
    {
        ESP_LOGI(TAG, "Password saved to NVS");
    }
    else if (strlen(password) == 0)
    {
        ESP_LOGI(TAG, "Password cleared from NVS");
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    return err;
}

void wifi_manager_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_wifi_init(&(wifi_init_config_t)WIFI_INIT_CONFIG_DEFAULT()));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    app_wifi_config_t stored_cfg = {0};
    if (wifi_config_load(&stored_cfg) == ESP_OK)
    {
        ESP_LOGI(TAG, "Loaded WiFi config from NVS: ssid='%s'", stored_cfg.ssid);
        current_cfg = stored_cfg;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };

    strncpy((char *)wifi_config.sta.ssid, current_cfg.ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, current_cfg.password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
}