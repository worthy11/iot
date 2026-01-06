#include "nvs_utils.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "nvs_utils";

// WiFi credentials NVS functions
esp_err_t nvs_save_wifi_credentials(const char *ssid, const char *password)
{
    if (!ssid || !password)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS namespace for saving credentials: %d", err);
        return err;
    }

    // Save SSID
    err = nvs_set_str(handle, "ssid", ssid);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save SSID: %d", err);
        nvs_close(handle);
        return err;
    }

    // Save password
    err = nvs_set_str(handle, "pass", password);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save password: %d", err);
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "WiFi credentials saved to NVS: ssid='%s'", ssid);
    }

    return err;
}

esp_err_t nvs_clear_wifi_credentials(void)
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

    ESP_LOGI(TAG, "WiFi credentials cleared from NVS");
    return ESP_OK;
}

esp_err_t nvs_read_wifi_credentials(char *ssid, size_t ssid_len, char *password, size_t password_len)
{
    if (!ssid || !password || ssid_len == 0 || password_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return err;
    }

    size_t required_size = ssid_len;
    err = nvs_get_str(handle, "ssid", ssid, &required_size);
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return err;
    }

    required_size = password_len;
    err = nvs_get_str(handle, "pass", password, &required_size);
    nvs_close(handle);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "WiFi credentials loaded from NVS: ssid='%s'", ssid);
    }

    return err;
}

// Certificate NVS functions
esp_err_t nvs_save_root_ca(const char *root_ca_pem)
{
    if (!root_ca_pem)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(CERT_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS for writing Root CA");
        return err;
    }

    err = nvs_set_blob(handle, "root_ca", root_ca_pem, strlen(root_ca_pem) + 1);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
        ESP_LOGI(TAG, "Root CA saved to NVS");
    }

    nvs_close(handle);
    return err;
}

esp_err_t nvs_save_device_certificate(const char *device_cert_pem)
{
    if (!device_cert_pem)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(CERT_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS for writing Device Certificate");
        return err;
    }

    err = nvs_set_blob(handle, "device_cert", device_cert_pem, strlen(device_cert_pem) + 1);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
        ESP_LOGI(TAG, "Device Certificate saved to NVS");
    }

    nvs_close(handle);
    return err;
}

esp_err_t nvs_save_private_key(const char *private_key_pem)
{
    if (!private_key_pem)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(CERT_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS for writing Private Key");
        return err;
    }

    err = nvs_set_blob(handle, "priv_key", private_key_pem, strlen(private_key_pem) + 1);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
        ESP_LOGI(TAG, "Private Key saved to NVS");
    }

    nvs_close(handle);
    return err;
}

esp_err_t nvs_read_root_ca(char *root_ca_pem, size_t root_ca_len)
{
    if (!root_ca_pem || root_ca_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(CERT_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return err;
    }

    size_t required_size = root_ca_len;
    err = nvs_get_blob(handle, "root_ca", root_ca_pem, &required_size);
    nvs_close(handle);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Root CA loaded from NVS (%zu bytes)", required_size);
    }

    return err;
}

esp_err_t nvs_read_device_certificate(char *device_cert_pem, size_t device_cert_len)
{
    if (!device_cert_pem || device_cert_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(CERT_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return err;
    }

    size_t required_size = device_cert_len;
    err = nvs_get_blob(handle, "device_cert", device_cert_pem, &required_size);
    nvs_close(handle);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Device Certificate loaded from NVS (%zu bytes)", required_size);
    }

    return err;
}

esp_err_t nvs_read_private_key(char *private_key_pem, size_t private_key_len)
{
    if (!private_key_pem || private_key_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(CERT_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return err;
    }

    size_t required_size = private_key_len;
    err = nvs_get_blob(handle, "priv_key", private_key_pem, &required_size);
    nvs_close(handle);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Private Key loaded from NVS (%zu bytes)", required_size);
    }

    return err;
}

// Provisioning NVS functions
esp_err_t nvs_save_topic_id(const char *topic_id)
{
    if (!topic_id)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(PROVISIONING_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS for writing topic_id");
        return err;
    }

    err = nvs_set_str(handle, "topic_id", topic_id);
    if (err == ESP_OK)
    {
        err = nvs_commit(handle);
        ESP_LOGI(TAG, "Topic ID saved to NVS: %s", topic_id);
    }

    nvs_close(handle);
    return err;
}

esp_err_t nvs_read_topic_id(char *topic_id, size_t topic_id_len)
{
    if (!topic_id || topic_id_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(PROVISIONING_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        return err;
    }

    size_t required_size = topic_id_len;
    err = nvs_get_str(handle, "topic_id", topic_id, &required_size);
    nvs_close(handle);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Topic ID loaded from NVS: %s", topic_id);
    }

    return err;
}

bool nvs_topic_id_changed(const char *new_topic_id)
{
    if (!new_topic_id || strlen(new_topic_id) == 0)
    {
        return true; // Empty topic_id means needs provisioning
    }

    char stored_topic_id[64] = {0};
    esp_err_t err = nvs_read_topic_id(stored_topic_id, sizeof(stored_topic_id));

    if (err != ESP_OK)
    {
        // No stored topic_id, needs provisioning
        return true;
    }

    // Compare with stored value
    if (strcmp(new_topic_id, stored_topic_id) != 0)
    {
        ESP_LOGI(TAG, "Topic ID changed: %s -> %s", stored_topic_id, new_topic_id);
        return true;
    }

    return false;
}
