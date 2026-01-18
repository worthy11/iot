#include "http_manager.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "utils/nvs_utils.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "http_manager";

esp_err_t http_manager_perform_ota_update(const char *firmware_url, const char *firmware_version)
{
    if (firmware_url == NULL)
    {
        ESP_LOGE(TAG, "Invalid firmware URL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting OTA update");

    esp_http_client_config_t http_config = {
        .url = firmware_url,
        .timeout_ms = 30000,
        .buffer_size = 4096,                        // Increase buffer size for HTTP headers and data
        .buffer_size_tx = 4096,                     // Increase TX buffer size for long URLs and headers
        .crt_bundle_attach = esp_crt_bundle_attach, // Use embedded certificate bundle for TLS verification
    };

    esp_https_ota_config_t ota_config = {0};
    ota_config.http_config = &http_config;

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Starting OTA download and flash...");

    while (1)
    {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS)
        {
            break;
        }
    }

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "OTA download and flash successful");

        err = esp_https_ota_finish(https_ota_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(err));
            return err;
        }

        ESP_LOGI(TAG, "OTA update completed successfully");
        return ESP_OK;
    }
    else
    {
        ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        return err;
    }
}
