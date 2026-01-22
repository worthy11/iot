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

    // OTA verification and marking as valid is handled in event_manager_init()
    // after NVS is initialized, so we can check for pending_ota flag

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize NVS utils (creates mutex for NVS operations)
    ret = nvs_utils_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize NVS utils: %s", esp_err_to_name(ret));
    }

    // Initialize SPIFFS filesystem
    ret = fs_utils_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize filesystem: %s", esp_err_to_name(ret));
    }

    event_manager_init();
}
