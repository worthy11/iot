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
#include "nvs.h"
#include "nvs_flash.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_STATUS_BIT BIT0

static const char *TAG = "wifi_manager";

typedef struct
{
    char ssid[32];
    char password[64];
} app_wifi_config_t;

#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK

static app_wifi_config_t g_current_cfg = {0};
static bool s_wifi_core_initialized = false; // tracks one-time network/event loop init
static bool g_current_cfg_valid = false;     // true when we have a loaded/saved config

#define WIFI_CONFIG_NAMESPACE "wifi_cfg"

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

static bool wifi_config_is_valid(const app_wifi_config_t *cfg)
{
    if (!cfg)
        return false;
    return true;
}

static esp_err_t wifi_config_save(const app_wifi_config_t *cfg)
{
    if (!cfg)
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;

    err = nvs_set_str(handle, "ssid", cfg->ssid);
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return err;
    }

    err = nvs_set_str(handle, "pass", cfg->password);
    if (err != ESP_OK)
    {
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
        ESP_LOGI(TAG, "WiFi disconnected. Retrying connection to the AP ssid='%s'", g_current_cfg_valid ? g_current_cfg.ssid : "(unknown)");
        event_manager_clear_bits(EVENT_BIT_WIFI_STATUS);
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi connected. Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        event_manager_set_bits(EVENT_BIT_WIFI_STATUS);
    }
}

static void wifi_credential_clear_task(void *pvParameters)
{
    TaskHandle_t task_handle = xTaskGetCurrentTaskHandle();
    event_manager_register_notification(task_handle, EVENT_BIT_WIFI_CLEARED);

    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(
            EVENT_BIT_WIFI_CLEARED,
            true,  // Clear on exit
            false, // Wait for any
            portMAX_DELAY);

        if (bits & EVENT_BIT_WIFI_CLEARED)
        {
            ESP_LOGI(TAG, "Clearing WiFi credentials (requested via event bit)");
            esp_err_t err = wifi_manager_clear_credentials();
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to clear WiFi credentials: %s", esp_err_to_name(err));
            }
        }
    }
}

const char *wifi_manager_get_current_ssid(void)
{
    return g_current_cfg_valid ? g_current_cfg.ssid : NULL;
}

const char *wifi_manager_get_current_password(void)
{
    return g_current_cfg_valid ? g_current_cfg.password : NULL;
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

    memset(&g_current_cfg, 0, sizeof(g_current_cfg));
    g_current_cfg_valid = false;

    event_manager_clear_bits(EVENT_BIT_WIFI_STATUS);
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    wifi_manager_init();

    ESP_LOGI(TAG, "WiFi credentials cleared successfully");
    return ESP_OK;
}

esp_err_t wifi_manager_save_credentials(const char *ssid, const char *password)
{
    if (!ssid || !password)
    {
        ESP_LOGE(TAG, "Invalid SSID or password (NULL pointer)");
        return ESP_ERR_INVALID_ARG;
    }

    app_wifi_config_t cfg = {0};
    strncpy(cfg.ssid, ssid, sizeof(cfg.ssid) - 1);
    strncpy(cfg.password, password, sizeof(cfg.password) - 1);

    if (!wifi_config_is_valid(&cfg))
    {
        ESP_LOGE(TAG, "WiFi config validation failed (ssid='%s')", ssid);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = wifi_config_save(&cfg);
    if (err == ESP_OK)
    {
        if (strlen(ssid) > 0)
        {
            ESP_LOGI(TAG, "WiFi credentials saved to NVS: ssid='%s'", ssid);
            g_current_cfg = cfg;
            g_current_cfg_valid = true;
        }
        else
        {
            ESP_LOGI(TAG, "WiFi credentials cleared (empty SSID saved)");
            memset(&g_current_cfg, 0, sizeof(g_current_cfg));
            g_current_cfg_valid = false;
        }

        wifi_manager_init();
    }
    else
    {
        ESP_LOGE(TAG, "Failed to save WiFi credentials: %s", esp_err_to_name(err));
    }
    return err;
}

void wifi_manager_init(void)
{
    if (!s_wifi_core_initialized)
    {
        ESP_LOGI(TAG, "core init");

        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();

        ESP_ERROR_CHECK(esp_wifi_init(&(wifi_init_config_t)WIFI_INIT_CONFIG_DEFAULT()));

        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

        s_wifi_core_initialized = true;

        xTaskCreate(
            wifi_credential_clear_task,
            "wifi_clear_cred",
            4096,
            NULL,
            5,
            NULL);
    }

    app_wifi_config_t stored_cfg = {0};
    bool use_stored = false;
    if (wifi_config_load(&stored_cfg) == ESP_OK && wifi_config_is_valid(&stored_cfg))
    {
        use_stored = true;
        ESP_LOGI(TAG, "Using WiFi config from NVS: ssid='%s'", stored_cfg.ssid);
        g_current_cfg = stored_cfg;
        g_current_cfg_valid = true;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };

    const char *ssid = use_stored ? stored_cfg.ssid : "";
    const char *pass = use_stored ? stored_cfg.password : "";

    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);

    if (s_wifi_core_initialized)
    {
        esp_wifi_stop();
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (use_stored)
    {
        ESP_LOGI(TAG, "WiFi (re)start complete; attempting connection");
    }
    else
    {
        ESP_LOGI(TAG, "WiFi started without credentials; waiting for provisioning");
    }
}