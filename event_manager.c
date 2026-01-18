#include "event_manager.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>
#include <math.h>

#include "ble/ble_manager.h"
#include "ble/command_service.h"
#include "wifi/wifi_manager.h"
#include "mqtt/mqtt_manager.h"
#include "mqtt/http_manager.h"
#include "hardware/hardware_manager.h"
#include "hardware/display/display_driver.h"
#include "utils/nvs_utils.h"
#include "utils/fs_utils.h"
#include "esp_sleep.h"
#include "freertos/semphr.h"
#include "esp_system.h"

#define GATT_SERVER_TIMEOUT_MS (10 * 60 * 1000)
#define PAIRING_TIMEOUT_MS (5 * 60 * 1000)
#define ADVERTISING_INTERVAL_MS (60 * 1000)
#define PH_CONFIRMATION_TIMEOUT_MS (30 * 1000)
#define CONNECTION_TIMEOUT_MS (60 * 1000)
#define EVENT_MANAGER_NVS_NAMESPACE "event_mgr"

static const char *TAG = "event_manager";
static EventGroupHandle_t s_event_group = NULL;

static TimerHandle_t ble_timer = NULL;
static TimerHandle_t publish_timer = NULL;
static TimerHandle_t temp_reading_timer = NULL;
static TimerHandle_t feeding_timer = NULL;

static uint32_t publish_interval_sec = 0;
static uint32_t g_temp_reading_interval_sec = 0;
static uint32_t g_feeding_interval_sec = 0;

static time_t g_last_publish_time = 0;
static time_t g_last_feed_time = 0;
static time_t g_last_temp_measurement_time = 0;

static int32_t activity_counter = 0;
static SemaphoreHandle_t activity_counter_mutex = NULL;

typedef struct
{
    TaskHandle_t task_handle;
    EventBits_t event_bits;
} notification_registration_t;

#define MAX_NOTIFICATIONS 10
static notification_registration_t notification_registrations[MAX_NOTIFICATIONS];
static int num_notifications = 0;

EventBits_t event_manager_set_bits(EventBits_t bits)
{
    EventBits_t result = xEventGroupSetBits(s_event_group, bits);

    for (int i = 0; i < num_notifications; i++)
    {
        if (notification_registrations[i].task_handle != NULL &&
            (bits & notification_registrations[i].event_bits))
        {
            xTaskNotify(notification_registrations[i].task_handle, 1, eSetBits);
        }
    }

    return result;
}

EventBits_t event_manager_clear_bits(EventBits_t bits)
{
    EventBits_t result = xEventGroupClearBits(s_event_group, bits);

    for (int i = 0; i < num_notifications; i++)
    {
        if (notification_registrations[i].task_handle != NULL &&
            (bits & notification_registrations[i].event_bits))
        {
            xTaskNotify(notification_registrations[i].task_handle, 1, eSetBits);
        }
    }

    return result;
}

EventBits_t event_manager_get_bits(void)
{
    return xEventGroupGetBits(s_event_group);
}

EventBits_t event_manager_wait_bits(EventBits_t bits_to_wait_for,
                                    bool clear_on_exit,
                                    bool wait_for_all,
                                    TickType_t timeout_ms)
{
    return xEventGroupWaitBits(
        s_event_group,
        bits_to_wait_for,
        clear_on_exit ? bits_to_wait_for : 0, // Clear bits on exit if requested
        wait_for_all ? pdTRUE : pdFALSE,      // Wait for all or any
        timeout_ms);
}

void event_manager_register_notification(TaskHandle_t task_handle, EventBits_t bits)
{
    notification_registrations[num_notifications].task_handle = task_handle;
    notification_registrations[num_notifications].event_bits = bits;
    num_notifications++;
}

uint32_t event_manager_get_passkey(void)
{
    return ble_manager_get_passkey();
}

static void publish_timer_callback(TimerHandle_t xTimer)
{
    event_manager_set_bits(EVENT_BIT_PUBLISH_SCHEDULED);
}

static void ble_connection_timer_callback(TimerHandle_t xTimer)
{
    event_manager_set_bits(EVENT_BIT_BLE_ADVERTISING);
}

static void temp_reading_timer_callback(TimerHandle_t xTimer)
{
    event_manager_set_bits(EVENT_BIT_TEMP_SCHEDULED);
}

static void feeding_timer_callback(TimerHandle_t xTimer)
{
    event_manager_set_bits(EVENT_BIT_FEED_SCHEDULED);
}

static void load_intervals(void)
{
    // Load intervals and last times from NVS
    esp_err_t err;
    size_t size;

    size = sizeof(uint32_t);
    err = nvs_load_blob(EVENT_MANAGER_NVS_NAMESPACE, "temp_int", &g_temp_reading_interval_sec, &size);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to load temp_interval: %s", esp_err_to_name(err));
        g_temp_reading_interval_sec = 0;
    }

    size = sizeof(uint32_t);
    err = nvs_load_blob(EVENT_MANAGER_NVS_NAMESPACE, "feed_int", &g_feeding_interval_sec, &size);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to load feeding_interval: %s", esp_err_to_name(err));
        g_feeding_interval_sec = 0;
    }

    size = sizeof(time_t);
    err = nvs_load_blob(EVENT_MANAGER_NVS_NAMESPACE, "last_feed", &g_last_feed_time, &size);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to load last_feed_time: %s", esp_err_to_name(err));
        g_last_feed_time = 0;
    }
    else if (size != sizeof(time_t))
    {
        ESP_LOGW(TAG, "last_feed_time size mismatch: expected %zu, got %zu. Resetting to 0", sizeof(time_t), size);
        g_last_feed_time = 0;
        // Clear the corrupted value
        nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "last_feed", &g_last_feed_time, sizeof(time_t));
    }
    else if (g_last_feed_time > 0 && g_last_feed_time < 946684800) // Year 2000 timestamp - invalid if before this (but 0 is valid)
    {
        ESP_LOGW(TAG, "last_feed_time is invalid (too old): %ld. Resetting to 0", (long)g_last_feed_time);
        g_last_feed_time = 0;
        // Clear the invalid value
        nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "last_feed", &g_last_feed_time, sizeof(time_t));
    }

    size = sizeof(time_t);
    err = nvs_load_blob(EVENT_MANAGER_NVS_NAMESPACE, "last_temp", &g_last_temp_measurement_time, &size);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to load last_temp_time: %s", esp_err_to_name(err));
        g_last_temp_measurement_time = 0;
    }
    else if (size != sizeof(time_t))
    {
        ESP_LOGW(TAG, "last_temp_time size mismatch: expected %zu, got %zu. Resetting to 0", sizeof(time_t), size);
        g_last_temp_measurement_time = 0;
        // Clear the corrupted value
        nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "last_temp", &g_last_temp_measurement_time, sizeof(time_t));
    }
    else if (g_last_temp_measurement_time > 0 && g_last_temp_measurement_time < 946684800) // Year 2000 timestamp - invalid if before this (but 0 is valid)
    {
        ESP_LOGW(TAG, "last_temp_time is invalid (too old): %ld. Resetting to 0", (long)g_last_temp_measurement_time);
        g_last_temp_measurement_time = 0;
        // Clear the invalid value
        nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "last_temp", &g_last_temp_measurement_time, sizeof(time_t));
    }

    size = sizeof(uint32_t);
    err = nvs_load_blob(EVENT_MANAGER_NVS_NAMESPACE, "publish_int", &publish_interval_sec, &size);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to load publish_interval: %s", esp_err_to_name(err));
        publish_interval_sec = 0;
    }

    size = sizeof(time_t);
    err = nvs_load_blob(EVENT_MANAGER_NVS_NAMESPACE, "last_publish", &g_last_publish_time, &size);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to load last_publish_time: %s", esp_err_to_name(err));
        g_last_publish_time = 0;
    }
    else if (size != sizeof(time_t))
    {
        ESP_LOGW(TAG, "last_publish_time size mismatch: expected %zu, got %zu. Resetting to 0", sizeof(time_t), size);
        g_last_publish_time = 0;
        // Clear the corrupted value
        nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "last_publish", &g_last_publish_time, sizeof(time_t));
    }
    else if (g_last_publish_time > 0 && g_last_publish_time < 946684800) // Year 2000 timestamp - invalid if before this (but 0 is valid)
    {
        ESP_LOGW(TAG, "last_publish_time is invalid (too old): %ld. Resetting to 0", (long)g_last_publish_time);
        g_last_publish_time = 0;
        // Clear the invalid value
        nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "last_publish", &g_last_publish_time, sizeof(time_t));
    }

    ESP_LOGI(TAG, "Intervals and times loaded from NVS: temp_interval=%lu, feed_interval=%lu, publish_interval=%lu, last_feed=%ld, last_temp=%ld, last_publish=%ld",
             (unsigned long)g_temp_reading_interval_sec, (unsigned long)g_feeding_interval_sec,
             (unsigned long)publish_interval_sec, (long)g_last_feed_time,
             (long)g_last_temp_measurement_time, (long)g_last_publish_time);

    time_t current_time = time(NULL);

    // Set temperature reading timer based on remaining time from last measurement
    if (g_temp_reading_interval_sec > 0 && temp_reading_timer != NULL)
    {
        if (g_last_temp_measurement_time > 0 && current_time > 1000000000) // Valid system time
        {
            time_t elapsed = current_time - g_last_temp_measurement_time;
            if (elapsed < (time_t)g_temp_reading_interval_sec)
            {
                // Calculate remaining time until next reading
                uint32_t remaining_sec = g_temp_reading_interval_sec - (uint32_t)elapsed;
                ESP_LOGI(TAG, "Setting temp timer to remaining time: %lu seconds", (unsigned long)remaining_sec);
                xTimerChangePeriod(temp_reading_timer, pdMS_TO_TICKS(remaining_sec * 1000), 0);
                xTimerStart(temp_reading_timer, 0);
            }
            else
            {
                // Interval has already passed, trigger immediately
                ESP_LOGI(TAG, "Temp interval already passed, triggering immediately");
                TickType_t period_ticks = pdMS_TO_TICKS(g_temp_reading_interval_sec * 1000);
                xTimerChangePeriod(temp_reading_timer, period_ticks, portMAX_DELAY);
                xTimerStart(temp_reading_timer, portMAX_DELAY);
                event_manager_set_bits(EVENT_BIT_TEMP_SCHEDULED);
            }
        }
        else
        {
            // No previous measurement or invalid time, set full interval
            TickType_t period_ticks = pdMS_TO_TICKS(g_temp_reading_interval_sec * 1000);
            xTimerChangePeriod(temp_reading_timer, period_ticks, portMAX_DELAY);
            xTimerStart(temp_reading_timer, portMAX_DELAY);
        }
    }

    // Set feeding timer based on remaining time from last feed
    if (g_feeding_interval_sec > 0 && feeding_timer != NULL)
    {
        if (g_last_feed_time > 0 && current_time > 1000000000) // Valid system time
        {
            time_t elapsed = current_time - g_last_feed_time;
            if (elapsed < (time_t)g_feeding_interval_sec)
            {
                // Calculate remaining time until next feed
                uint32_t remaining_sec = g_feeding_interval_sec - (uint32_t)elapsed;
                ESP_LOGI(TAG, "Setting feed timer to remaining time: %lu seconds", (unsigned long)remaining_sec);
                xTimerChangePeriod(feeding_timer, pdMS_TO_TICKS(remaining_sec * 1000), 0);
                xTimerStart(feeding_timer, 0);
            }
            else
            {
                // Interval has already passed, trigger immediately
                ESP_LOGI(TAG, "Feed interval already passed, triggering immediately");
                TickType_t period_ticks = pdMS_TO_TICKS(g_feeding_interval_sec * 1000);
                xTimerChangePeriod(feeding_timer, period_ticks, portMAX_DELAY);
                xTimerStart(feeding_timer, portMAX_DELAY);
                event_manager_set_bits(EVENT_BIT_FEED_SCHEDULED);
            }
        }
        else
        {
            // No previous feed or invalid time, set full interval
            TickType_t period_ticks = pdMS_TO_TICKS(g_feeding_interval_sec * 1000);
            xTimerChangePeriod(feeding_timer, period_ticks, portMAX_DELAY);
            xTimerStart(feeding_timer, portMAX_DELAY);
        }
    }

    // Set publish timer based on remaining time from last publish
    if (publish_interval_sec > 0 && publish_timer != NULL)
    {
        if (g_last_publish_time > 0 && current_time > 1000000000) // Valid system time
        {
            time_t elapsed = current_time - g_last_publish_time;
            if (elapsed < (time_t)publish_interval_sec)
            {
                // Calculate remaining time until next publish
                uint32_t remaining_sec = publish_interval_sec - (uint32_t)elapsed;
                ESP_LOGI(TAG, "Setting publish timer to remaining time: %lu seconds", (unsigned long)remaining_sec);
                xTimerChangePeriod(publish_timer, pdMS_TO_TICKS(remaining_sec * 1000), 0);
                xTimerStart(publish_timer, 0);
            }
            else
            {
                // Interval has already passed, trigger immediately
                ESP_LOGI(TAG, "Publish interval already passed, triggering immediately");
                TickType_t period_ticks = pdMS_TO_TICKS(publish_interval_sec * 1000);
                xTimerChangePeriod(publish_timer, period_ticks, portMAX_DELAY);
                xTimerStart(publish_timer, portMAX_DELAY);
                event_manager_set_bits(EVENT_BIT_PUBLISH_SCHEDULED);
            }
        }
        else
        {
            // No previous publish or invalid time, set full interval
            TickType_t period_ticks = pdMS_TO_TICKS(publish_interval_sec * 1000);
            xTimerChangePeriod(publish_timer, period_ticks, portMAX_DELAY);
            xTimerStart(publish_timer, portMAX_DELAY);
        }
    }
}

void event_manager_set_feeding_interval(uint32_t feed_interval_seconds)
{
    g_feeding_interval_sec = feed_interval_seconds;
    nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "feed_int", &g_feeding_interval_sec, sizeof(uint32_t));

    if (feed_interval_seconds == 0)
    {
        xTimerStop(feeding_timer, portMAX_DELAY);
        ESP_LOGI(TAG, "Feeding timer stopped");
    }
    else
    {
        time_t current_time = time(NULL);
        if (g_last_feed_time > 0 && current_time > 1000000000) // Valid system time and last feed time
        {
            time_t elapsed = current_time - g_last_feed_time;
            if (elapsed < (time_t)feed_interval_seconds)
            {
                // Calculate remaining time until next feed
                uint32_t remaining_sec = feed_interval_seconds - (uint32_t)elapsed;
                ESP_LOGI(TAG, "Setting feed timer to remaining time: %lu seconds", (unsigned long)remaining_sec);
                TickType_t period_ticks = pdMS_TO_TICKS(remaining_sec * 1000);
                xTimerChangePeriod(feeding_timer, period_ticks, portMAX_DELAY);
                xTimerStart(feeding_timer, portMAX_DELAY);
            }
            else
            {
                // Interval has already passed, trigger immediately
                ESP_LOGI(TAG, "Feed interval already passed, triggering immediately");
                TickType_t period_ticks = pdMS_TO_TICKS(feed_interval_seconds * 1000);
                xTimerChangePeriod(feeding_timer, period_ticks, portMAX_DELAY);
                xTimerStart(feeding_timer, portMAX_DELAY);
                event_manager_set_bits(EVENT_BIT_FEED_SCHEDULED);
            }
        }
        else
        {
            // No previous feed or invalid time, set full interval
            TickType_t period_ticks = pdMS_TO_TICKS(feed_interval_seconds * 1000);
            xTimerChangePeriod(feeding_timer, period_ticks, portMAX_DELAY);
            xTimerStart(feeding_timer, portMAX_DELAY);
            ESP_LOGI(TAG, "Feeding timer set to %lu seconds (auto-reload)", (unsigned long)feed_interval_seconds);
        }
    }
}

void event_manager_set_temp_reading_interval(uint32_t temp_interval_seconds)
{
    g_temp_reading_interval_sec = temp_interval_seconds;
    nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "temp_int", &g_temp_reading_interval_sec, sizeof(uint32_t));

    if (temp_reading_timer == NULL)
    {
        ESP_LOGE(TAG, "Temperature reading timer not initialized");
        return;
    }

    if (temp_interval_seconds == 0)
    {
        xTimerStop(temp_reading_timer, portMAX_DELAY);
        ESP_LOGI(TAG, "Temperature reading timer stopped");
    }
    else
    {
        time_t current_time = time(NULL);
        if (g_last_temp_measurement_time > 0 && current_time > 1000000000) // Valid system time and last temp time
        {
            time_t elapsed = current_time - g_last_temp_measurement_time;
            if (elapsed < (time_t)temp_interval_seconds)
            {
                // Calculate remaining time until next reading
                uint32_t remaining_sec = temp_interval_seconds - (uint32_t)elapsed;
                ESP_LOGI(TAG, "Setting temp timer to remaining time: %lu seconds", (unsigned long)remaining_sec);
                TickType_t period_ticks = pdMS_TO_TICKS(remaining_sec * 1000);
                xTimerChangePeriod(temp_reading_timer, period_ticks, portMAX_DELAY);
                xTimerStart(temp_reading_timer, portMAX_DELAY);
            }
            else
            {
                // Interval has already passed, trigger immediately
                ESP_LOGI(TAG, "Temp interval already passed, triggering immediately");
                TickType_t period_ticks = pdMS_TO_TICKS(temp_interval_seconds * 1000);
                xTimerChangePeriod(temp_reading_timer, period_ticks, portMAX_DELAY);
                xTimerStart(temp_reading_timer, portMAX_DELAY);
                event_manager_set_bits(EVENT_BIT_TEMP_SCHEDULED);
            }
        }
        else
        {
            // No previous measurement or invalid time, set full interval
            TickType_t period_ticks = pdMS_TO_TICKS(temp_interval_seconds * 1000);
            xTimerChangePeriod(temp_reading_timer, period_ticks, portMAX_DELAY);
            xTimerStart(temp_reading_timer, portMAX_DELAY);
            ESP_LOGI(TAG, "Temperature reading timer set to %lu seconds (auto-reload)", (unsigned long)temp_interval_seconds);
        }
    }
}

void event_manager_set_publish_interval(int publish_frequency)
{
    publish_interval_sec = publish_frequency >= 0 ? (uint32_t)publish_frequency : 0;
    nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "publish_int", &publish_interval_sec, sizeof(uint32_t));

    if (publish_timer == NULL)
    {
        ESP_LOGE(TAG, "Publish timer not initialized");
        return;
    }

    if (publish_interval_sec == 0)
    {
        xTimerStop(publish_timer, portMAX_DELAY);
        ESP_LOGI(TAG, "Publish timer disabled (never)");
    }
    else
    {
        time_t current_time = time(NULL);
        if (g_last_publish_time > 0 && current_time > 1000000000) // Valid system time and last publish time
        {
            time_t elapsed = current_time - g_last_publish_time;
            if (elapsed < (time_t)publish_interval_sec)
            {
                // Calculate remaining time until next publish
                uint32_t remaining_sec = publish_interval_sec - (uint32_t)elapsed;
                ESP_LOGI(TAG, "Setting publish timer to remaining time: %lu seconds", (unsigned long)remaining_sec);
                TickType_t period_ticks = pdMS_TO_TICKS(remaining_sec * 1000);
                xTimerChangePeriod(publish_timer, period_ticks, portMAX_DELAY);
                xTimerStart(publish_timer, portMAX_DELAY);
            }
            else
            {
                // Interval has already passed, trigger immediately
                ESP_LOGI(TAG, "Publish interval already passed, triggering immediately");
                TickType_t period_ticks = pdMS_TO_TICKS(publish_interval_sec * 1000);
                xTimerChangePeriod(publish_timer, period_ticks, portMAX_DELAY);
                xTimerStart(publish_timer, portMAX_DELAY);
                event_manager_set_bits(EVENT_BIT_PUBLISH_SCHEDULED);
            }
        }
        else
        {
            // No previous publish or invalid time, set full interval
            TickType_t period_ticks = pdMS_TO_TICKS(publish_interval_sec * 1000);
            xTimerChangePeriod(publish_timer, period_ticks, portMAX_DELAY);
            xTimerStart(publish_timer, portMAX_DELAY);
            ESP_LOGI(TAG, "Publish timer set to %lu seconds (auto-reload)", (unsigned long)publish_interval_sec);
        }
    }
}

uint32_t event_manager_get_feeding_interval(void)
{
    return g_feeding_interval_sec;
}

uint32_t event_manager_get_temp_reading_interval(void)
{
    return g_temp_reading_interval_sec;
}

uint32_t event_manager_get_publish_interval(void)
{
    return publish_interval_sec;
}

uint32_t event_manager_get_temp_timer_remaining_sec(void)
{
    if (temp_reading_timer == NULL || g_temp_reading_interval_sec == 0)
    {
        return 0;
    }

    // Always use the actual timer expiry time when timer is active
    // This ensures the reported remaining time matches what the timer actually has
    if (xTimerIsTimerActive(temp_reading_timer) == pdTRUE)
    {
        TickType_t expiry_time = xTimerGetExpiryTime(temp_reading_timer);
        TickType_t current_ticks = xTaskGetTickCount();

        TickType_t remaining_ticks;
        if (expiry_time > current_ticks)
        {
            remaining_ticks = expiry_time - current_ticks;
        }
        else
        {
            return 0;
        }

        uint32_t remaining_ms = (uint32_t)(remaining_ticks * portTICK_PERIOD_MS);
        return remaining_ms / 1000;
    }

    // Timer not active, calculate based on last measurement time as fallback
    time_t current_time = time(NULL);
    if (current_time >= 1000000000 && g_last_temp_measurement_time > 0)
    {
        time_t elapsed = current_time - g_last_temp_measurement_time;
        if (elapsed < (time_t)g_temp_reading_interval_sec)
        {
            return g_temp_reading_interval_sec - (uint32_t)elapsed;
        }
    }

    return 0;
}

uint32_t event_manager_get_feed_timer_remaining_sec(void)
{
    if (feeding_timer == NULL || g_feeding_interval_sec == 0)
    {
        return 0;
    }

    // Always use the actual timer expiry time when timer is active
    // This ensures the reported remaining time matches what the timer actually has
    if (xTimerIsTimerActive(feeding_timer) == pdTRUE)
    {
        TickType_t expiry_time = xTimerGetExpiryTime(feeding_timer);
        TickType_t current_ticks = xTaskGetTickCount();

        TickType_t remaining_ticks;
        if (expiry_time > current_ticks)
        {
            remaining_ticks = expiry_time - current_ticks;
        }
        else
        {
            return 0;
        }

        uint32_t remaining_ms = (uint32_t)(remaining_ticks * portTICK_PERIOD_MS);
        return remaining_ms / 1000;
    }

    // Timer not active, calculate based on last feed time as fallback
    time_t current_time = time(NULL);
    if (current_time >= 1000000000 && g_last_feed_time > 0)
    {
        time_t elapsed = current_time - g_last_feed_time;
        if (elapsed < (time_t)g_feeding_interval_sec)
        {
            return g_feeding_interval_sec - (uint32_t)elapsed;
        }
    }

    return 0;
}

static uint32_t get_publish_timer_remaining_sec(void)
{
    if (publish_timer == NULL || publish_interval_sec == 0)
    {
        return 0;
    }

    // Always use the actual timer expiry time when timer is active
    // This ensures the reported remaining time matches what the timer actually has
    if (xTimerIsTimerActive(publish_timer) == pdTRUE)
    {
        TickType_t expiry_time = xTimerGetExpiryTime(publish_timer);
        TickType_t current_ticks = xTaskGetTickCount();

        TickType_t remaining_ticks;
        if (expiry_time > current_ticks)
        {
            remaining_ticks = expiry_time - current_ticks;
        }
        else
        {
            return 0;
        }

        uint32_t remaining_ms = (uint32_t)(remaining_ticks * portTICK_PERIOD_MS);
        return remaining_ms / 1000;
    }

    // Timer not active, calculate based on last publish time as fallback
    time_t current_time = time(NULL);
    if (current_time >= 1000000000 && g_last_publish_time > 0)
    {
        time_t elapsed = current_time - g_last_publish_time;
        if (elapsed < (time_t)publish_interval_sec)
        {
            return publish_interval_sec - (uint32_t)elapsed;
        }
    }

    return 0;
}

static uint32_t get_ble_timer_remaining_sec(void)
{
    if (ble_timer == NULL)
    {
        return 0;
    }

    if (xTimerIsTimerActive(ble_timer) == pdFALSE)
    {
        return 0;
    }

    TickType_t expiry_time = xTimerGetExpiryTime(ble_timer);
    TickType_t current_time = xTaskGetTickCount();

    // Handle timer overflow
    TickType_t remaining_ticks;
    if (expiry_time > current_time)
    {
        remaining_ticks = expiry_time - current_time;
    }
    else
    {
        // Timer has already expired or will expire very soon
        return 0;
    }

    // Convert ticks to seconds (portTICK_PERIOD_MS is in milliseconds)
    uint32_t remaining_ms = (uint32_t)(remaining_ticks * portTICK_PERIOD_MS);
    return remaining_ms / 1000;
}

static void activity_counter_increment(void)
{
    if (activity_counter_mutex != NULL && xSemaphoreTake(activity_counter_mutex, portMAX_DELAY) == pdTRUE)
    {
        activity_counter++;
        ESP_LOGI(TAG, "Activity counter incremented to %ld", (long)activity_counter);
        xSemaphoreGive(activity_counter_mutex);
    }
}

static void activity_counter_decrement(void)
{
    if (activity_counter_mutex != NULL && xSemaphoreTake(activity_counter_mutex, portMAX_DELAY) == pdTRUE)
    {
        if (activity_counter > 0)
        {
            activity_counter--;
        }
        ESP_LOGI(TAG, "Activity counter decremented to %ld", (long)activity_counter);
        xSemaphoreGive(activity_counter_mutex);
    }
}

void event_manager_activity_counter_increment(void)
{
    activity_counter_increment();
}

void event_manager_activity_counter_decrement(void)
{
    activity_counter_decrement();
}

bool event_manager_is_activity_running(void)
{
    bool running = false;
    if (activity_counter_mutex != NULL && xSemaphoreTake(activity_counter_mutex, portMAX_DELAY) == pdTRUE)
    {
        running = activity_counter > 0;
        xSemaphoreGive(activity_counter_mutex);
    }
    return running;
}

// BLE task
void event_manager_advertising_task(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(EVENT_BIT_BLE_ADVERTISING, false, false, portMAX_DELAY);
        if (bits & EVENT_BIT_BLE_ADVERTISING && !((bits & EVENT_BIT_PAIRING_MODE) || (bits & EVENT_BIT_PAIRING_SUCCESS) || (bits & EVENT_BIT_PASSKEY_DISPLAY)))
        {
            ble_start_advertising();

            bits = event_manager_wait_bits(EVENT_BIT_BLE_CONNECTED, true, false, pdMS_TO_TICKS(GATT_SERVER_TIMEOUT_MS));
            if ((bits & EVENT_BIT_BLE_CONNECTED))
            {
                ESP_LOGI(TAG, "BLE connected");

                bits = event_manager_wait_bits(EVENT_BIT_BLE_DISCONNECTED, true, false, portMAX_DELAY);
                if (bits & EVENT_BIT_BLE_DISCONNECTED)
                {
                    ESP_LOGI(TAG, "BLE disconnected");
                }
            }
            else
            {
                ESP_LOGI(TAG, "BLE connection timeout");
            }

            if (!((bits & EVENT_BIT_PAIRING_MODE) || (bits & EVENT_BIT_PAIRING_SUCCESS) || (bits & EVENT_BIT_PASSKEY_DISPLAY)))
            {
                ble_stop_advertising();
                xTimerReset(ble_timer, portMAX_DELAY);
                event_manager_set_bits(EVENT_BIT_DEEP_SLEEP);
            }
        }
        event_manager_clear_bits(EVENT_BIT_BLE_ADVERTISING);
    }
}

void event_manager_pairing_task(void *pvParameters)
{
    (void)pvParameters;

    TaskHandle_t task_handle = xTaskGetCurrentTaskHandle();
    event_manager_register_notification(task_handle, EVENT_BIT_PAIRING_MODE | EVENT_BIT_PASSKEY_DISPLAY | EVENT_BIT_PAIRING_SUCCESS | EVENT_BIT_BLE_DISCONNECTED);
    uint32_t notif = 0;

    while (1)
    {
        EventBits_t bits = event_manager_get_bits();
        if (bits & EVENT_BIT_PAIRING_MODE)
        {
            ESP_LOGI(TAG, "Pairing mode active");
            if (!(bits & EVENT_BIT_BLE_ADVERTISING))
                ble_start_advertising();
            hardware_manager_display_event("pairing_mode_screen", NAN);

            xTaskNotifyWait(0, UINT32_MAX, &notif, pdMS_TO_TICKS(PAIRING_TIMEOUT_MS));
            event_manager_clear_bits(EVENT_BIT_PAIRING_MODE);
            if (notif)
            {
                notif = 0;
                continue;
            }
            else
            {
                ESP_LOGI(TAG, "Pairing timeout");
            }
        }
        else if (bits & EVENT_BIT_PASSKEY_DISPLAY)
        {
            hardware_manager_display_event("passkey", (float)event_manager_get_passkey());
            xTaskNotifyWait(0, UINT32_MAX, &notif, pdMS_TO_TICKS(PAIRING_TIMEOUT_MS));
            event_manager_clear_bits(EVENT_BIT_PASSKEY_DISPLAY);
            if (notif)
            {
                notif = 0;
                continue;
            }
            else
            {
                ESP_LOGI(TAG, "Pairing timeout");
            }
        }
        else if (bits & EVENT_BIT_PAIRING_SUCCESS)
        {
            ESP_LOGI(TAG, "Pairing successful");
            hardware_manager_display_update();

            event_manager_clear_bits(EVENT_BIT_PAIRING_SUCCESS);
            bits = event_manager_wait_bits(EVENT_BIT_BLE_DISCONNECTED, false, false, portMAX_DELAY);

            ble_stop_advertising();
            activity_counter_decrement();
            xTimerReset(ble_timer, portMAX_DELAY);
        }
        else if (bits & EVENT_BIT_BLE_DISCONNECTED && !((bits & EVENT_BIT_BLE_ADVERTISING)))
        {
            ESP_LOGI(TAG, "Pairing mode off");
            ble_stop_advertising();
            hardware_manager_display_update();
            xTaskNotifyWait(0, UINT32_MAX, &notif, portMAX_DELAY);
            notif = 0;
            continue;
        }
        else
        {
            xTaskNotifyWait(0, UINT32_MAX, &notif, portMAX_DELAY);
            notif = 0;
            continue;
        }
    }
}

static void event_manager_provisioning_task(void *pvParameters)
{
    (void)pvParameters;
    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(EVENT_BIT_PROVISIONING_CHANGED, true, false, portMAX_DELAY);
        if (bits & EVENT_BIT_PROVISIONING_CHANGED)
        {
            activity_counter_increment();
            ESP_LOGI(TAG, "Provisioning changed, reloading configurations");
            mqtt_manager_load_config();
            wifi_manager_load_config();
            activity_counter_decrement();
        }
    }
}

static void event_manager_action_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t notif = 0;
    TaskHandle_t task_handle = xTaskGetCurrentTaskHandle();
    event_manager_register_notification(task_handle, EVENT_BIT_PH_CONFIRMED);

    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(
            EVENT_BIT_TEMP_SCHEDULED | EVENT_BIT_PH_SCHEDULED | EVENT_BIT_FEED_SCHEDULED,
            false, // Don't clear on exit - we'll clear individually
            false, // Wait for any
            portMAX_DELAY);

        activity_counter_increment();
        if (bits & EVENT_BIT_TEMP_SCHEDULED)
        {
            hardware_manager_display_event("temp_measurement_screen", NAN);
            float temp = hardware_manager_measure_temp();
            if (!isnan(temp))
            {
                mqtt_manager_enqueue_temperature(temp);
                ble_manager_notify_temperature(temp);
                g_last_temp_measurement_time = time(NULL);
                nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "last_temp", &g_last_temp_measurement_time, sizeof(time_t));
            }
            else
            {
                mqtt_manager_enqueue_log("hardware_error", "temperature_read_failed");
            }
            event_manager_clear_bits(EVENT_BIT_TEMP_SCHEDULED);

            // Reset timer period to full interval after measurement to ensure proper auto-reload timing
            if (temp_reading_timer != NULL && g_temp_reading_interval_sec > 0)
            {
                TickType_t period_ticks = pdMS_TO_TICKS(g_temp_reading_interval_sec * 1000);
                xTimerChangePeriod(temp_reading_timer, period_ticks, portMAX_DELAY);
                xTimerStart(temp_reading_timer, portMAX_DELAY);
            }
        }

        if (bits & EVENT_BIT_PH_SCHEDULED)
        {
            EventBits_t ph_bits = event_manager_get_bits();
            bool ph_confirmed = (ph_bits & EVENT_BIT_PH_CONFIRMED) != 0;
            if (!ph_confirmed)
            {
                hardware_manager_display_event("ph_confirmation_screen", NAN);
                xTaskNotifyWait(0, UINT32_MAX, &notif, pdMS_TO_TICKS(PH_CONFIRMATION_TIMEOUT_MS));
                if (!notif) // PH_CONFIRMED not set
                {
                    ESP_LOGI(TAG, "pH confirmation timeout");
                    event_manager_clear_bits(EVENT_BIT_PH_SCHEDULED);
                    continue;
                }
                else
                {
                    ESP_LOGI(TAG, "pH confirmation received");
                }
            }
            hardware_manager_display_event("ph_measurement_screen", NAN);
            float ph_value = hardware_manager_measure_ph();
            event_manager_clear_bits(EVENT_BIT_PH_SCHEDULED);
            event_manager_clear_bits(EVENT_BIT_PH_CONFIRMED);
            if (!isnan(ph_value))
            {
                mqtt_manager_enqueue_ph(ph_value);
                ble_manager_notify_ph(ph_value);
            }
            else
            {
                mqtt_manager_enqueue_log("hardware_error", "ph_read_failed");
            }
        }

        if (bits & EVENT_BIT_FEED_SCHEDULED)
        {
            bool feed_successful = hardware_manager_feed();
            mqtt_manager_enqueue_feed(feed_successful);
            if (!feed_successful)
                event_manager_set_bits(EVENT_BIT_PUBLISH_SCHEDULED);
            ble_manager_notify_feed(feed_successful);
            g_last_feed_time = time(NULL);
            display_set_feed_time(g_last_feed_time);
            nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "last_feed", &g_last_feed_time, sizeof(time_t));
            event_manager_clear_bits(EVENT_BIT_FEED_SCHEDULED);
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
        hardware_manager_display_update();
        activity_counter_decrement();
    }
}

// Display task
void event_manager_display_task(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(EVENT_BIT_DISPLAY_NEXT | EVENT_BIT_DISPLAY_PREV | EVENT_BIT_DISPLAY_CONFIRM, true, false, portMAX_DELAY);

        hardware_manager_display_wake();
        if (bits & EVENT_BIT_DISPLAY_NEXT)
        {
            hardware_manager_display_next();
        }
        else if (bits & EVENT_BIT_DISPLAY_PREV)
        {
            hardware_manager_display_prev();
        }
        else if (bits & EVENT_BIT_DISPLAY_CONFIRM)
        {
            hardware_manager_display_confirm();
        }
    }
}

// Connection task
void event_manager_connection_task(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(EVENT_BIT_PUBLISH_SCHEDULED | EVENT_BIT_OTA_UPDATE, true, false, portMAX_DELAY);
        if (bits & EVENT_BIT_PUBLISH_SCHEDULED)
        {
            ESP_LOGI(TAG, "Publish scheduled");
            activity_counter_increment();
            wifi_manager_start();
            mqtt_manager_start();
            bits = event_manager_wait_bits(EVENT_BIT_WIFI_STATUS | EVENT_BIT_MQTT_STATUS, false, true, pdMS_TO_TICKS(CONNECTION_TIMEOUT_MS));
            if (!(bits & EVENT_BIT_WIFI_STATUS) || !(bits & EVENT_BIT_MQTT_STATUS))
            {
                ESP_LOGW(TAG, "Publish failed - WiFi=%d, MQTT=%d",
                         (bits & EVENT_BIT_WIFI_STATUS) != 0,
                         (bits & EVENT_BIT_MQTT_STATUS) != 0);
            }
            else
            {
                ESP_LOGI(TAG, "Connection successful");
                mqtt_manager_publish();
                g_last_publish_time = time(NULL);
                nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "last_publish", &g_last_publish_time, sizeof(time_t));
            }

            mqtt_manager_stop();
            wifi_manager_stop();
            activity_counter_decrement();
            event_manager_clear_bits(EVENT_BIT_PUBLISH_SCHEDULED);
        }
        else if (bits & EVENT_BIT_OTA_UPDATE)
        {
            ESP_LOGI(TAG, "OTA update triggered");
            activity_counter_increment();

            // Turn on WiFi
            wifi_manager_start();

            // Wait for WiFi to be ready
            bits = event_manager_wait_bits(EVENT_BIT_WIFI_STATUS, false, true, pdMS_TO_TICKS(CONNECTION_TIMEOUT_MS));

            if (!(bits & EVENT_BIT_WIFI_STATUS))
            {
                ESP_LOGE(TAG, "OTA update failed - WiFi=%d",
                         (bits & EVENT_BIT_WIFI_STATUS) != 0);
                activity_counter_decrement();
                wifi_manager_stop();
                event_manager_clear_bits(EVENT_BIT_OTA_UPDATE);
                continue;
            }

            ESP_LOGI(TAG, "WiFi ready, starting firmware update...");

            const char *firmware_url = command_service_get_firmware_url();
            if (firmware_url == NULL || strlen(firmware_url) == 0)
            {
                ESP_LOGE(TAG, "No firmware URL available");
                wifi_manager_stop();
                activity_counter_decrement();
                event_manager_clear_bits(EVENT_BIT_OTA_UPDATE);
                continue;
            }

            esp_err_t err = http_manager_perform_ota_update(firmware_url, NULL);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(err));
                wifi_manager_stop();
                activity_counter_decrement();
                event_manager_clear_bits(EVENT_BIT_OTA_UPDATE);
                continue;
            }

            wifi_manager_stop();
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        }
    }
}

// Sleep task
void event_manager_sleep_task(void *pvParameters)
{
    (void)pvParameters;
    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(EVENT_BIT_DEEP_SLEEP, true, false, pdMS_TO_TICKS(portMAX_DELAY));

        if (bits & EVENT_BIT_DEEP_SLEEP)
        {
            if (event_manager_is_activity_running())
            {
                ESP_LOGI(TAG, "Deep sleep requested but activities are running, waiting...");
                continue;
            }

            uint32_t temp_remaining = event_manager_get_temp_timer_remaining_sec();
            uint32_t feed_remaining = event_manager_get_feed_timer_remaining_sec();
            uint32_t ble_remaining = get_ble_timer_remaining_sec();
            uint32_t publish_remaining = get_publish_timer_remaining_sec();

            ESP_LOGI(TAG, "Timer remaining times - Temp: %lu sec, Feed: %lu sec, BLE: %lu sec, Publish: %lu sec",
                     (unsigned long)temp_remaining,
                     (unsigned long)feed_remaining,
                     (unsigned long)ble_remaining,
                     (unsigned long)publish_remaining);

            uint32_t shortest_remaining = UINT32_MAX;

            if (temp_remaining > 0 && temp_remaining < shortest_remaining)
            {
                shortest_remaining = temp_remaining;
            }
            if (feed_remaining > 0 && feed_remaining < shortest_remaining)
            {
                shortest_remaining = feed_remaining;
            }
            if (ble_remaining > 0 && ble_remaining < shortest_remaining)
            {
                shortest_remaining = ble_remaining;
            }
            if (publish_remaining > 0 && publish_remaining < shortest_remaining)
            {
                shortest_remaining = publish_remaining;
            }

            uint64_t sleep_duration_us;

            if (shortest_remaining != UINT32_MAX && shortest_remaining > 0)
            {
                sleep_duration_us = (uint64_t)shortest_remaining * 1000000ULL;
                ESP_LOGI(TAG, "Using shortest timer: %lu seconds", (unsigned long)shortest_remaining);
            }
            else
            {
                sleep_duration_us = 60ULL * 60ULL * 1000000ULL; // 1 hour
                ESP_LOGI(TAG, "No active timers, defaulting to 1 hour (%lld microseconds)",
                         (long long)sleep_duration_us);
            }

            esp_sleep_enable_timer_wakeup(sleep_duration_us);
            esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON); // Keep RTC peripherals powered
            esp_sleep_enable_ext0_wakeup(GPIO_CONFIRM_BUTTON, 0);            // 0 = wake on LOW (button pressed)

            time_t sleep_timestamp = time(NULL);
            if (sleep_timestamp > 1000000000)
            {
                vTaskDelay(pdMS_TO_TICKS(500));
            }

            esp_deep_sleep_start();
        }
    }
}

void event_manager_init(void)
{
    s_event_group = xEventGroupCreate();
    activity_counter_mutex = xSemaphoreCreateMutex();
    if (activity_counter_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create activity counter mutex");
    }
    num_notifications = 0;

    hardware_manager_init();
    wifi_manager_init();
    ble_manager_init();
    mqtt_manager_init();

    esp_sleep_wakeup_cause_t wake_reason = esp_sleep_get_wakeup_cause();

    // Wake display only on normal boot or button press, not on timer wake
    if (wake_reason == ESP_SLEEP_WAKEUP_UNDEFINED)
    {
        ESP_LOGI(TAG, "Normal boot (not from deep sleep)");
        hardware_manager_display_wake();
        hardware_manager_display_update();
    }
    else if (wake_reason == ESP_SLEEP_WAKEUP_EXT0)
    {
        ESP_LOGI(TAG, "Woke up from deep sleep - button pressed (GPIO %d)", GPIO_CONFIRM_BUTTON);
        hardware_manager_display_wake();
        hardware_manager_display_update();
    }
    else if (wake_reason == ESP_SLEEP_WAKEUP_TIMER)
    {
        ESP_LOGI(TAG, "Woke up from deep sleep - timer expired (display will remain off)");
    }

    xTaskCreate(
        event_manager_advertising_task,
        "adv_coordinator",
        4 * 1024,
        NULL,
        2,
        NULL);

    xTaskCreate(
        event_manager_pairing_task,
        "pair_coordinator",
        4 * 1024,
        NULL,
        2,
        NULL);

    xTaskCreate(
        event_manager_provisioning_task,
        "provision_coordinator",
        4 * 1024,
        NULL,
        2,
        NULL);

    xTaskCreate(
        event_manager_action_task,
        "action_coordinator",
        4 * 1024,
        NULL,
        3,
        NULL);

    xTaskCreate(
        event_manager_display_task,
        "display_coordinator",
        2 * 1024,
        NULL,
        3,
        NULL);

    xTaskCreate(
        event_manager_sleep_task,
        "sleep_coordinator",
        4 * 1024,
        NULL,
        1,
        NULL);

    xTaskCreate(
        event_manager_connection_task,
        "connection_coordinator",
        8 * 1024,
        NULL,
        2,
        NULL);

    publish_timer = xTimerCreate(
        "publish_timer",
        pdMS_TO_TICKS(1000), // Initial period (will be changed by setter)
        pdTRUE,              // Auto-reload: timer will reset itself when it expires
        NULL,                // Timer ID (not used)
        publish_timer_callback);

    temp_reading_timer = xTimerCreate(
        "temp_reading_timer",
        pdMS_TO_TICKS(1000), // Initial period (will be changed by setter)
        pdTRUE,              // Auto-reload: timer will reset itself when it expires
        NULL,                // Timer ID (not used)
        temp_reading_timer_callback);

    feeding_timer = xTimerCreate(
        "feeding_timer",
        pdMS_TO_TICKS(1000), // Initial period (will be changed by setter)
        pdTRUE,              // Auto-reload: timer will reset itself when it expires
        NULL,                // Timer ID (not used)
        feeding_timer_callback);

    ble_timer = xTimerCreate(
        "ble_connection_timer",
        pdMS_TO_TICKS(ADVERTISING_INTERVAL_MS),
        pdFALSE,
        NULL,
        ble_connection_timer_callback);

    if (publish_timer == NULL || temp_reading_timer == NULL || feeding_timer == NULL)
    {
        ESP_LOGE(TAG, "Failed to create timers");
    }
    else
    {
        ESP_LOGI(TAG, "All timers created successfully");
    }

    load_intervals();
    event_manager_set_bits(EVENT_BIT_BLE_ADVERTISING);

    ESP_LOGI(TAG, "Event manager initialized");
}