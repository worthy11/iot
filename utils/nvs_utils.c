#include "nvs_utils.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "nvs_utils";

esp_err_t nvs_save_blob(const char *namespace, const char *key, const void *value, size_t len)
{
    if (!namespace || !key)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (len > 0 && !value)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", namespace, esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, key, value, len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save blob '%s' in namespace '%s': %s", key, namespace, esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t nvs_load_blob(const char *namespace, const char *key, void *value, size_t *len)
{
    if (!namespace || !key || !value || !len || *len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return err;
    }

    size_t required_size = *len;
    err = nvs_get_blob(handle, key, value, &required_size);
    nvs_close(handle);

    if (err == ESP_OK)
    {
        *len = required_size;
    }

    return err;
}