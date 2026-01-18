#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_console.h"
#include "driver/uart.h"
#include "esp_ota_ops.h"

#include "event_manager.h"
#include "utils/fs_utils.h"
#include "utils/nvs_utils.h"

static const char *TAG = "main";

void app_main(void)
{
    // Check OTA rollback state - must be done early
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();

    if (running != NULL)
    {
        ESP_LOGI(TAG, "Running from partition: %s (0x%lx)", running->label, (unsigned long)running->address);
    }
    if (boot != NULL)
    {
        ESP_LOGI(TAG, "Boot partition: %s (0x%lx)", boot->label, (unsigned long)boot->address);
    }

    esp_ota_img_states_t ota_state;
    esp_err_t ota_state_err = esp_ota_get_state_partition(running, &ota_state);

    ESP_LOGI(TAG, "OTA state check: err=%s, state=%d (PENDING_VERIFY=%d)",
             esp_err_to_name(ota_state_err), ota_state, ESP_OTA_IMG_PENDING_VERIFY);

    // Track if we're in PENDING_VERIFY state (before marking valid)
    bool was_pending_verify = (ota_state_err == ESP_OK && ota_state == ESP_OTA_IMG_PENDING_VERIFY);

    if (was_pending_verify)
    {
        ESP_LOGI(TAG, "New OTA firmware detected - running verification...");

        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "OTA firmware verification passed - marking as valid");
        esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to mark app as valid: %s", esp_err_to_name(ret));
        }
        else
        {
            ESP_LOGI(TAG, "OTA firmware marked as valid - rollback cancelled");
        }
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // After NVS init, save pending version if we just marked OTA as valid
    if (was_pending_verify)
    {
        // Try to load and save pending version (NVS is now initialized)
        char pending_version[32] = {0};
        size_t version_size = sizeof(pending_version);
        esp_err_t load_err = nvs_load_blob("firmware", "pending_version", pending_version, &version_size);
        if (load_err == ESP_OK)
        {
            // Ensure null termination
            pending_version[sizeof(pending_version) - 1] = '\0';

            // Save as confirmed version
            esp_err_t save_err = nvs_save_blob("firmware", "version", pending_version, strlen(pending_version) + 1);
            if (save_err == ESP_OK)
            {
                ESP_LOGI(TAG, "Firmware version confirmed and saved: %s", pending_version);
            }
            else
            {
                ESP_LOGW(TAG, "Failed to save confirmed firmware version: %s", esp_err_to_name(save_err));
            }
        }
        else
        {
            ESP_LOGW(TAG, "No pending firmware version found in NVS");
        }
    }

    // Initialize SPIFFS filesystem
    ret = fs_utils_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize filesystem: %s", esp_err_to_name(ret));
    }

    event_manager_init();
}
