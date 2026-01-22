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
static bool wifi_started = false;

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

    size_t size;

    size = sizeof(out_cfg->ssid);
    esp_err_t err = nvs_load_blob(WIFI_CONFIG_NAMESPACE, "ssid", out_cfg->ssid, &size);
    if (err != ESP_OK)
        return err;

    size = sizeof(out_cfg->password);
    err = nvs_load_blob(WIFI_CONFIG_NAMESPACE, "pass", out_cfg->password, &size);
    if (err != ESP_OK)
        return err;

    return ESP_OK;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        // Only attempt connection if credentials are loaded
        if (current_cfg.ssid[0] != '\0')
        {
            esp_wifi_connect();
            ESP_LOGI(TAG, "WiFi started. Connecting...");
        }
        else
        {
            ESP_LOGW(TAG, "WiFi started but no credentials loaded, cannot connect");
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGI(TAG, "Disconnected from the AP (reason: %d). Retrying...", disconn->reason);

        event_manager_clear_bits(EVENT_BIT_WIFI_STATUS);

        // Always retry connection after disconnection
        vTaskDelay(pdMS_TO_TICKS(1000));
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
    if (wifi_started)
    {
        ESP_LOGI(TAG, "WiFi already started");
        return ESP_OK;
    }

    // Load WiFi credentials on every start
    wifi_manager_load_config();

    esp_err_t err = esp_wifi_start();
    if (err == ESP_OK)
    {
        esp_err_t pm_err = esp_wifi_set_ps(WIFI_PS_NONE);
        if (pm_err != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to set WiFi power save mode: %s", esp_err_to_name(pm_err));
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(50));
            wifi_ps_type_t ps_type;
            esp_wifi_get_ps(&ps_type);
            if (ps_type != WIFI_PS_NONE)
            {
                ESP_LOGW(TAG, "WiFi power save verification failed after start, retrying...");
                esp_wifi_set_ps(WIFI_PS_NONE);
            }
        }
        wifi_started = true;
    }
    return err;
}

void wifi_manager_stop(void)
{
    if (!wifi_started)
    {
        ESP_LOGI(TAG, "WiFi already stopped");
        return;
    }
    else if (event_manager_get_bits() & EVENT_BIT_OTA_UPDATE)
    {
        ESP_LOGI(TAG, "Cannot stop WiFi during OTA update");
        return;
    }

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_wifi_stop();
    wifi_started = false;
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
    esp_err_t err = wifi_config_load(&cfg);

    if (err != ESP_OK)
    {
        // Only warn if it's an actual error, not just "not found" (expected before provisioning)
        if (err != ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGW(TAG, "Failed to load WiFi credentials from NVS: %s", esp_err_to_name(err));
        }
        // Don't clear current_cfg if load fails - keep existing config
        return err;
    }

    // Update current config
    current_cfg = cfg;

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
            .pmf_cfg = {
                .capable = true,
                .required = false},
            .listen_interval = 1, // Reduced from 3 to 1 - check AP more frequently to prevent probe timeouts
        },
    };

    strncpy((char *)wifi_config.sta.ssid, current_cfg.ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, current_cfg.password, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    esp_err_t pm_err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (pm_err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to set WiFi power save mode: %s", esp_err_to_name(pm_err));
    }
    else
    {
        ESP_LOGI(TAG, "WiFi power save mode set to NONE (always active)");
        wifi_ps_type_t ps_type;
        esp_wifi_get_ps(&ps_type);
        if (ps_type != WIFI_PS_NONE)
        {
            ESP_LOGW(TAG, "WiFi power save was not set correctly, retrying...");
            esp_wifi_set_ps(WIFI_PS_NONE);
        }
    }

    wifi_ps_type_t ps_type;
    esp_wifi_get_ps(&ps_type);
    if (ps_type != WIFI_PS_NONE)
    {
        ESP_LOGW(TAG, "WiFi power save was not set correctly, retrying...");
        esp_wifi_set_ps(WIFI_PS_NONE);
    }
}