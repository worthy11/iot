#include "aquarium_data.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "aquarium_data";
static const char *NVS_NAMESPACE = "aquarium_data";

static aquarium_data_t g_data = {
    .temperature = 0.0f,
    .ph = 0.0f,
    .last_feed_time = 0,
    .next_feed_time = 0,
    .display_contrast = 128,
    .font_size = 1,
    .line_height = 10,
    .temperature_display_enabled = true,
    .ph_display_enabled = true,
    .last_feeding_display_enabled = true,
    .next_feeding_display_enabled = true};

static SemaphoreHandle_t data_mutex = NULL;

static esp_err_t save_to_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, "data", &g_data, sizeof(aquarium_data_t));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save data: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t load_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to open NVS (first run?): %s", esp_err_to_name(err));
        return err;
    }

    size_t required_size = sizeof(aquarium_data_t);
    err = nvs_get_blob(handle, "data", &g_data, &required_size);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to load data: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Loaded aquarium data from NVS");
    return ESP_OK;
}

void aquarium_data_init(void)
{
    if (data_mutex == NULL)
    {
        data_mutex = xSemaphoreCreateMutex();
        if (data_mutex == NULL)
        {
            ESP_LOGE(TAG, "Failed to create data mutex");
            return;
        }
    }

    load_from_nvs();

    aquarium_data_set_font_size(1);

    ESP_LOGI(TAG, "Aquarium data initialized");
}

void aquarium_data_get(aquarium_data_t *data)
{
    if (!data)
        return;

    if (data_mutex && xSemaphoreTake(data_mutex, portMAX_DELAY) == pdTRUE)
    {
        memcpy(data, &g_data, sizeof(aquarium_data_t));
        xSemaphoreGive(data_mutex);
    }
}

void aquarium_data_update_temperature(float temp)
{
    if (data_mutex && xSemaphoreTake(data_mutex, portMAX_DELAY) == pdTRUE)
    {
        g_data.temperature = temp;
        xSemaphoreGive(data_mutex);
    }
}

void aquarium_data_update_ph(float ph)
{
    if (data_mutex && xSemaphoreTake(data_mutex, portMAX_DELAY) == pdTRUE)
    {
        g_data.ph = ph;
        xSemaphoreGive(data_mutex);
    }
}

void aquarium_data_update_last_feed(time_t feed_time)
{
    if (data_mutex && xSemaphoreTake(data_mutex, portMAX_DELAY) == pdTRUE)
    {
        g_data.last_feed_time = feed_time;
        save_to_nvs();
        xSemaphoreGive(data_mutex);
    }
}

void aquarium_data_update_next_feed(time_t next_time)
{
    if (data_mutex && xSemaphoreTake(data_mutex, portMAX_DELAY) == pdTRUE)
    {
        g_data.next_feed_time = next_time;
        save_to_nvs();
        xSemaphoreGive(data_mutex);
    }
}

void aquarium_data_set_contrast(uint8_t contrast)
{
    if (data_mutex && xSemaphoreTake(data_mutex, portMAX_DELAY) == pdTRUE)
    {
        g_data.display_contrast = contrast;
        save_to_nvs();
        xSemaphoreGive(data_mutex);
    }
}

uint8_t aquarium_data_get_contrast(void)
{
    uint8_t contrast = 32;
    if (data_mutex && xSemaphoreTake(data_mutex, portMAX_DELAY) == pdTRUE)
    {
        contrast = g_data.display_contrast;
        xSemaphoreGive(data_mutex);
    }
    return contrast;
}

void aquarium_data_set_font_size(uint8_t font_size)
{
    if (data_mutex && xSemaphoreTake(data_mutex, portMAX_DELAY) == pdTRUE)
    {
        g_data.font_size = font_size;
        save_to_nvs();
        xSemaphoreGive(data_mutex);
    }
}

uint8_t aquarium_data_get_font_size(void)
{
    uint8_t font_size = 1;
    if (data_mutex && xSemaphoreTake(data_mutex, portMAX_DELAY) == pdTRUE)
    {
        font_size = g_data.font_size;
        xSemaphoreGive(data_mutex);
    }
    return font_size;
}

uint8_t aquarium_data_get_line_height(void)
{
    uint8_t font_size = 1;
    if (data_mutex && xSemaphoreTake(data_mutex, portMAX_DELAY) == pdTRUE)
    {
        font_size = g_data.font_size;
        xSemaphoreGive(data_mutex);
    }
    return font_size * 8 + 2;
}

void aquarium_data_set_display_enabled(bool temp, bool ph, bool last_feed, bool next_feed)
{
    if (data_mutex && xSemaphoreTake(data_mutex, portMAX_DELAY) == pdTRUE)
    {
        g_data.temperature_display_enabled = temp;
        g_data.ph_display_enabled = ph;
        g_data.last_feeding_display_enabled = last_feed;
        g_data.next_feeding_display_enabled = next_feed;
        save_to_nvs();
        xSemaphoreGive(data_mutex);
    }
}
