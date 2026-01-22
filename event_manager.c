#include "event_manager.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>
#include <time.h>

#include "ble/ble_manager.h"
#include "ble/command_service.h"
#include "ble/gap.h"
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
#include "esp_heap_caps.h"
#include "freertos/timers.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_sntp.h"
#include <math.h>
#include <sys/time.h>

#define GATT_SERVER_TIMEOUT_MS (10 * 1000)
#define PAIRING_TIMEOUT_MS (5 * 60 * 1000)
#define ADVERTISING_INTERVAL_MS (60 * 1000)
#define PH_CONFIRMATION_TIMEOUT_MS (30 * 1000)
#define CONNECTION_TIMEOUT_MS (15 * 1000)
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

static float temp_lower = -INFINITY;
static float temp_upper = INFINITY;
static float ph_lower = -INFINITY;
static float ph_upper = INFINITY;

// Time synchronization variables
static int64_t g_synced_time_ms = 0;    // Last synced time in milliseconds (Unix timestamp)
static int64_t g_synced_uptime_us = 0;  // Uptime in microseconds when time was synced
static bool g_time_synced = false;      // Whether time has been synced
static bool g_sntp_initialized = false; // Whether SNTP has been initialized

static int32_t activity_counter = 0;
static SemaphoreHandle_t activity_counter_mutex = NULL;

static void sntp_sync_time_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "SNTP time synchronized: %ld", (long)tv->tv_sec);

    // Update internal time sync variables when SNTP syncs
    int64_t current_uptime_us = esp_timer_get_time();
    g_synced_time_ms = (int64_t)tv->tv_sec * 1000LL + (int64_t)tv->tv_usec / 1000LL;
    g_synced_uptime_us = current_uptime_us;
    g_time_synced = true;

    ESP_LOGI(TAG, "Updated time sync: synced_time_ms=%lld, synced_uptime_us=%lld",
             (long long)g_synced_time_ms, (long long)g_synced_uptime_us);

    event_manager_set_bits(EVENT_BIT_TIME_SYNC);
}

static void initialize_sntp(void)
{
    if (g_sntp_initialized)
    {
        ESP_LOGD(TAG, "SNTP already initialized, skipping");
        return;
    }

    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(sntp_sync_time_cb);
    esp_sntp_init();

    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    g_sntp_initialized = true;
}

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

    size = sizeof(uint32_t);
    err = nvs_load_blob(EVENT_MANAGER_NVS_NAMESPACE, "publish_int", &publish_interval_sec, &size);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to load publish_interval: %s", esp_err_to_name(err));
        publish_interval_sec = 0;
    }

    ESP_LOGI(TAG, "Intervals loaded from NVS: temp_interval=%lu, feed_interval=%lu, publish_interval=%lu",
             (unsigned long)g_temp_reading_interval_sec, (unsigned long)g_feeding_interval_sec,
             (unsigned long)publish_interval_sec);

    typedef struct
    {
        uint32_t temp_remaining;
        uint32_t feed_remaining;
        uint32_t publish_remaining;
    } timer_remaining_data_t;

    timer_remaining_data_t timer_data;
    size_t timer_data_size = sizeof(timer_data);
    esp_err_t timer_load_err = nvs_load_blob(EVENT_MANAGER_NVS_NAMESPACE, "timer_remaining", &timer_data, &timer_data_size);

    bool use_saved_timers = (timer_load_err == ESP_OK && timer_data_size == sizeof(timer_data));

    if (use_saved_timers)
    {
        ESP_LOGI(TAG, "Loaded timer remaining values from NVS: temp=%lu sec, feed=%lu sec, publish=%lu sec",
                 (unsigned long)timer_data.temp_remaining,
                 (unsigned long)timer_data.feed_remaining,
                 (unsigned long)timer_data.publish_remaining);
    }

    if (g_temp_reading_interval_sec > 0 && temp_reading_timer != NULL)
    {
        uint32_t remaining_sec = 0;
        bool should_trigger = false;

        if (use_saved_timers)
        {
            if (timer_data.temp_remaining == 0)
            {
                // Timer expired (remaining = 0) - trigger immediately
                should_trigger = true;
                remaining_sec = g_temp_reading_interval_sec;
            }
            else if (timer_data.temp_remaining > 0)
            {
                remaining_sec = timer_data.temp_remaining;
                if (remaining_sec >= g_temp_reading_interval_sec)
                {
                    should_trigger = true;
                    remaining_sec = g_temp_reading_interval_sec;
                }
            }
        }
        else
        {
            // No saved timer or invalid, set full interval
            remaining_sec = g_temp_reading_interval_sec;
        }

        if (should_trigger)
        {
            ESP_LOGI(TAG, "Temp timer expired, triggering immediately");
            TickType_t period_ticks = pdMS_TO_TICKS(remaining_sec * 1000);
            xTimerChangePeriod(temp_reading_timer, period_ticks, portMAX_DELAY);
            xTimerStart(temp_reading_timer, portMAX_DELAY);
            event_manager_set_bits(EVENT_BIT_TEMP_SCHEDULED);
        }
        else
        {
            ESP_LOGI(TAG, "Setting temp timer to remaining time: %lu seconds", (unsigned long)remaining_sec);
            xTimerChangePeriod(temp_reading_timer, pdMS_TO_TICKS(remaining_sec * 1000), 0);
            xTimerStart(temp_reading_timer, 0);
        }
    }

    if (g_feeding_interval_sec > 0 && feeding_timer != NULL)
    {
        uint32_t remaining_sec = 0;
        bool should_trigger = false;

        if (use_saved_timers && timer_data.feed_remaining > 0)
        {
            remaining_sec = timer_data.feed_remaining;
            if (remaining_sec >= g_feeding_interval_sec)
            {
                should_trigger = true;
                remaining_sec = g_feeding_interval_sec;
            }
        }
        else
        {
            // No saved timer or invalid, set full interval
            remaining_sec = g_feeding_interval_sec;
        }

        if (should_trigger)
        {
            ESP_LOGI(TAG, "Feed timer expired, triggering immediately");
            TickType_t period_ticks = pdMS_TO_TICKS(remaining_sec * 1000);
            xTimerChangePeriod(feeding_timer, period_ticks, portMAX_DELAY);
            xTimerStart(feeding_timer, portMAX_DELAY);
            event_manager_set_bits(EVENT_BIT_FEED_SCHEDULED);
        }
        else
        {
            ESP_LOGI(TAG, "Setting feed timer to remaining time: %lu seconds", (unsigned long)remaining_sec);
            xTimerChangePeriod(feeding_timer, pdMS_TO_TICKS(remaining_sec * 1000), 0);
            xTimerStart(feeding_timer, 0);
        }
    }

    // Set publish timer
    if (publish_interval_sec > 0 && publish_timer != NULL)
    {
        uint32_t remaining_sec = 0;
        bool should_trigger = false;

        if (use_saved_timers)
        {
            if (timer_data.publish_remaining == 0)
            {
                // Timer expired (remaining = 0) - trigger immediately
                should_trigger = true;
                remaining_sec = publish_interval_sec;
            }
            else if (timer_data.publish_remaining > 0)
            {
                remaining_sec = timer_data.publish_remaining;
                if (remaining_sec >= publish_interval_sec)
                {
                    should_trigger = true;
                    remaining_sec = publish_interval_sec;
                }
            }
        }
        else
        {
            // No saved timer or invalid, set full interval
            remaining_sec = publish_interval_sec;
        }

        if (should_trigger)
        {
            ESP_LOGI(TAG, "Publish timer expired, triggering immediately");
            TickType_t period_ticks = pdMS_TO_TICKS(remaining_sec * 1000);
            xTimerChangePeriod(publish_timer, period_ticks, portMAX_DELAY);
            xTimerStart(publish_timer, portMAX_DELAY);
            event_manager_set_bits(EVENT_BIT_PUBLISH_SCHEDULED);
        }
        else
        {
            ESP_LOGI(TAG, "Setting publish timer to remaining time: %lu seconds", (unsigned long)remaining_sec);
            xTimerChangePeriod(publish_timer, pdMS_TO_TICKS(remaining_sec * 1000), 0);
            xTimerStart(publish_timer, 0);
        }
    }
}

void event_manager_set_temp_lower(float threshold)
{
    temp_lower = threshold;
    nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "temp_lower", &temp_lower, sizeof(float));
}

void event_manager_set_temp_upper(float threshold)
{
    temp_upper = threshold;
    nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "temp_upper", &temp_upper, sizeof(float));
}

void event_manager_set_ph_lower(float threshold)
{
    ph_lower = threshold;
    nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "ph_lower", &ph_lower, sizeof(float));
}

void event_manager_set_ph_upper(float threshold)
{
    ph_upper = threshold;
    nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "ph_upper", &ph_upper, sizeof(float));
}

float event_manager_get_temp_lower(void)
{
    return temp_lower;
}

float event_manager_get_temp_upper(void)
{
    return temp_upper;
}

float event_manager_get_ph_lower(void)
{
    return ph_lower;
}

float event_manager_get_ph_upper(void)
{
    return ph_upper;
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
        TickType_t period_ticks = pdMS_TO_TICKS(feed_interval_seconds * 1000);
        xTimerChangePeriod(feeding_timer, period_ticks, portMAX_DELAY);
        xTimerStart(feeding_timer, portMAX_DELAY);
        ESP_LOGI(TAG, "Feeding timer set to %lu seconds (auto-reload)", (unsigned long)feed_interval_seconds);
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
        TickType_t period_ticks = pdMS_TO_TICKS(temp_interval_seconds * 1000);
        xTimerChangePeriod(temp_reading_timer, period_ticks, portMAX_DELAY);
        xTimerStart(temp_reading_timer, portMAX_DELAY);
        ESP_LOGI(TAG, "Temperature reading timer set to %lu seconds (auto-reload)", (unsigned long)temp_interval_seconds);
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
        TickType_t period_ticks = pdMS_TO_TICKS(publish_interval_sec * 1000);
        xTimerChangePeriod(publish_timer, period_ticks, portMAX_DELAY);
        xTimerStart(publish_timer, portMAX_DELAY);
        ESP_LOGI(TAG, "Publish timer set to %lu seconds (auto-reload)", (unsigned long)publish_interval_sec);
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

    return 0;
}

static uint32_t get_publish_timer_remaining_sec(void)
{
    if (publish_timer == NULL || publish_interval_sec == 0)
    {
        return 0;
    }

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

int64_t event_manager_get_current_timestamp_ms(void)
{
    if (g_time_synced)
    {
        // Return synced time + current uptime
        int64_t current_uptime_us = esp_timer_get_time();
        int64_t elapsed_ms = (current_uptime_us - g_synced_uptime_us) / 1000LL;
        return g_synced_time_ms + elapsed_ms;
    }
    else
    {
        // Time not synced yet, return uptime
        return esp_timer_get_time() / 1000LL;
    }
}

// BLE task
void event_manager_advertising_task(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(EVENT_BIT_BLE_ADVERTISING, true, false, portMAX_DELAY);
        if (bits & EVENT_BIT_BLE_ADVERTISING)
        {
            ble_start_advertising();
            activity_counter_increment();

            bits = event_manager_wait_bits(EVENT_BIT_BLE_CONNECTED, false, false, pdMS_TO_TICKS(GATT_SERVER_TIMEOUT_MS));
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

            ble_stop_advertising();
            activity_counter_decrement();

            if (ble_timer != NULL)
            {
                xTimerReset(ble_timer, portMAX_DELAY);
                xTimerStart(ble_timer, portMAX_DELAY);
            }

            event_manager_set_bits(EVENT_BIT_DEEP_SLEEP);
        }
    }
}

static void event_manager_provisioning_task(void *pvParameters)
{
    (void)pvParameters;
    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(EVENT_BIT_PAIRING_MODE_ON, false, false, portMAX_DELAY);
        if (bits & EVENT_BIT_PAIRING_MODE_ON)
        {
            ESP_LOGI(TAG, "Pairing mode on");
            ble_start_advertising();
            activity_counter_increment();
            hardware_manager_display_event("pairing_screen", NAN);

            bits = event_manager_wait_bits(EVENT_BIT_BLE_CONNECTED | EVENT_BIT_PAIRING_MODE_OFF, false, false, pdMS_TO_TICKS(PAIRING_TIMEOUT_MS));
            if (bits & EVENT_BIT_BLE_CONNECTED)
            {
                ESP_LOGI(TAG, "BLE connected");
                hardware_manager_display_update();
                bits = event_manager_wait_bits(EVENT_BIT_BLE_DISCONNECTED | EVENT_BIT_PAIRING_MODE_OFF, false, false, pdMS_TO_TICKS(portMAX_DELAY));
                if (bits & EVENT_BIT_BLE_DISCONNECTED)
                {
                    ESP_LOGI(TAG, "BLE disconnected");
                    event_manager_clear_bits(EVENT_BIT_PAIRING_MODE_ON);
                    event_manager_set_bits(EVENT_BIT_PAIRING_MODE_OFF);
                }
                else if (bits & EVENT_BIT_PAIRING_MODE_OFF)
                {
                    ESP_LOGI(TAG, "Pairing mode off");
                    bits = event_manager_wait_bits(EVENT_BIT_BLE_DISCONNECTED, false, false, pdMS_TO_TICKS(portMAX_DELAY));
                    if (bits & EVENT_BIT_BLE_DISCONNECTED)
                    {
                        ESP_LOGI(TAG, "BLE disconnected");
                        event_manager_clear_bits(EVENT_BIT_PAIRING_MODE_ON);
                        event_manager_set_bits(EVENT_BIT_PAIRING_MODE_OFF);
                    }
                }
            }
            else if (bits & EVENT_BIT_PAIRING_MODE_OFF)
            {
                ESP_LOGI(TAG, "Pairing mode off");
            }
            else
            {
                ESP_LOGI(TAG, "BLE connection timeout");
            }

            hardware_manager_display_update();
            ble_stop_advertising();
            activity_counter_decrement();
            event_manager_set_bits(EVENT_BIT_DEEP_SLEEP);
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

                if (temp < temp_lower)
                {
                    char value_str[32];
                    snprintf(value_str, sizeof(value_str), "%.2f", temp);
                    mqtt_manager_enqueue_log("temp_below", value_str);
                    event_manager_set_bits(EVENT_BIT_PUBLISH_SCHEDULED);
                }
                else if (temp > temp_upper)
                {
                    char value_str[32];
                    snprintf(value_str, sizeof(value_str), "%.2f", temp);
                    mqtt_manager_enqueue_log("temp_above", value_str);
                    event_manager_set_bits(EVENT_BIT_PUBLISH_SCHEDULED);
                }
            }
            else
            {
                mqtt_manager_enqueue_log("hardware_error", "temperature_read_failed");
            }
            event_manager_clear_bits(EVENT_BIT_TEMP_SCHEDULED);

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
                // Round pH to 2 decimal places
                float ph_rounded = roundf(ph_value * 100.0f) / 100.0f;
                mqtt_manager_enqueue_ph(ph_rounded);
                ble_manager_notify_ph(ph_value);

                // Check if threshold is exceeded
                if (ph_rounded < ph_lower)
                {
                    char value_str[32];
                    snprintf(value_str, sizeof(value_str), "%.2f", ph_rounded);
                    mqtt_manager_enqueue_log("ph_below", value_str);
                    event_manager_set_bits(EVENT_BIT_PUBLISH_SCHEDULED);
                }
                else if (ph_rounded > ph_upper)
                {
                    char value_str[32];
                    snprintf(value_str, sizeof(value_str), "%.2f", ph_rounded);
                    mqtt_manager_enqueue_log("ph_above", value_str);
                    event_manager_set_bits(EVENT_BIT_PUBLISH_SCHEDULED);
                }
            }
            else
            {
                mqtt_manager_enqueue_log("hardware_error", "ph_read_failed");
            }
        }

        if (bits & EVENT_BIT_FEED_SCHEDULED)
        {
            bool feed_successful = hardware_manager_feed();
            if (feed_successful)
            {
                mqtt_manager_enqueue_feed(true);
            }
            else
            {
                mqtt_manager_enqueue_feed(false);
                mqtt_manager_enqueue_log("hardware_error", "feed_failed");
            }

            if (!feed_successful)
                event_manager_set_bits(EVENT_BIT_PUBLISH_SCHEDULED);

            ble_manager_notify_feed(feed_successful);

            event_manager_clear_bits(EVENT_BIT_FEED_SCHEDULED);
            if (temp_reading_timer != NULL && g_temp_reading_interval_sec > 0)
            {
                TickType_t period_ticks = pdMS_TO_TICKS(g_feeding_interval_sec * 1000);
                xTimerChangePeriod(feeding_timer, period_ticks, portMAX_DELAY);
                xTimerStart(feeding_timer, portMAX_DELAY);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
        hardware_manager_display_update();
        activity_counter_decrement();
        event_manager_set_bits(EVENT_BIT_DEEP_SLEEP);
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
        EventBits_t bits = event_manager_wait_bits(EVENT_BIT_PUBLISH_SCHEDULED | EVENT_BIT_OTA_UPDATE | EVENT_BIT_TIME_SYNC, false, false, portMAX_DELAY);
        if (bits & EVENT_BIT_PUBLISH_SCHEDULED)
        {
            ESP_LOGI(TAG, "Publish scheduled");
            // Clear the bit immediately to prevent re-triggering during processing
            event_manager_clear_bits(EVENT_BIT_PUBLISH_SCHEDULED);
            activity_counter_increment();

            wifi_manager_start();

            bits = event_manager_wait_bits(EVENT_BIT_WIFI_STATUS, false, false, pdMS_TO_TICKS(CONNECTION_TIMEOUT_MS));
            if (!(bits & EVENT_BIT_WIFI_STATUS))
            {
                ESP_LOGW(TAG, "Publish failed - not connected to WiFi");
                activity_counter_decrement();
                wifi_manager_stop();
                continue;
            }

            mqtt_manager_start();

            bits = event_manager_wait_bits(EVENT_BIT_MQTT_STATUS, false, false, pdMS_TO_TICKS(CONNECTION_TIMEOUT_MS));
            if (!(bits & EVENT_BIT_WIFI_STATUS) || !(bits & EVENT_BIT_MQTT_STATUS))
            {
                ESP_LOGW(TAG, "Publish failed - not connected to MQTT");
                activity_counter_decrement();
                mqtt_manager_stop();
                wifi_manager_stop();
                event_manager_set_bits(EVENT_BIT_DEEP_SLEEP);
                continue;
            }

            ESP_LOGI(TAG, "Connection successful");
            mqtt_manager_publish();

            for (int i = 0; i < CONNECTION_TIMEOUT_MS / 1000; i++)
            {
                vTaskDelay(pdMS_TO_TICKS(1000));
                bits = event_manager_get_bits();
                if (!(bits & EVENT_BIT_WIFI_STATUS))
                {
                    ESP_LOGW(TAG, "WiFi disconnected during wait period, stopping early");
                    break;
                }
                if (bits & EVENT_BIT_OTA_UPDATE)
                {
                    ESP_LOGI(TAG, "OTA update triggered, stopping early");
                    continue;
                }
            }

            ESP_LOGI(TAG, "No longer receiving commands, closing connection");

            mqtt_manager_stop();

            // Only stop WiFi if it's still connected (avoid race condition with auto-reconnect)
            bits = event_manager_get_bits();
            if (bits & EVENT_BIT_WIFI_STATUS)
            {
                wifi_manager_stop();
            }
            else
            {
                ESP_LOGI(TAG, "WiFi already disconnected, skipping stop");
            }
            if (publish_timer != NULL && publish_interval_sec > 0)
            {
                TickType_t period_ticks = pdMS_TO_TICKS(publish_interval_sec * 1000);
                xTimerChangePeriod(publish_timer, period_ticks, portMAX_DELAY);
                xTimerStart(publish_timer, portMAX_DELAY);
            }
            activity_counter_decrement();
        }
        else if (bits & EVENT_BIT_TIME_SYNC)
        {
            ESP_LOGI(TAG, "Time sync requested");
            event_manager_clear_bits(EVENT_BIT_TIME_SYNC);
            activity_counter_increment();

            wifi_manager_start();

            bits = event_manager_wait_bits(EVENT_BIT_WIFI_STATUS, false, false, pdMS_TO_TICKS(CONNECTION_TIMEOUT_MS));
            if (!(bits & EVENT_BIT_WIFI_STATUS))
            {
                ESP_LOGW(TAG, "Time sync failed - not connected to WiFi");
                activity_counter_decrement();
                wifi_manager_stop();
                continue;
            }

            initialize_sntp();

            bits = event_manager_wait_bits(EVENT_BIT_TIME_SYNC, true, false, pdMS_TO_TICKS(30000));
            if (bits & EVENT_BIT_TIME_SYNC)
            {
                ESP_LOGI(TAG, "Time synchronized successfully");
            }
            else
            {
                ESP_LOGW(TAG, "Time synchronization timeout");
            }

            wifi_manager_stop();
            activity_counter_decrement();
        }
        else if (bits & EVENT_BIT_OTA_UPDATE)
        {
            ESP_LOGI(TAG, "OTA update triggered");
            activity_counter_increment();

            wifi_manager_start();

            bits = event_manager_wait_bits(EVENT_BIT_WIFI_STATUS, false, false, pdMS_TO_TICKS(CONNECTION_TIMEOUT_MS));

            if (!(bits & EVENT_BIT_WIFI_STATUS))
            {
                ESP_LOGE(TAG, "OTA update failed - WiFi=%d",
                         (bits & EVENT_BIT_WIFI_STATUS) != 0);
                activity_counter_decrement();
                wifi_manager_stop();
                event_manager_clear_bits(EVENT_BIT_OTA_UPDATE);
                event_manager_set_bits(EVENT_BIT_DEEP_SLEEP);
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
                event_manager_set_bits(EVENT_BIT_DEEP_SLEEP);
                continue;
            }

            ESP_LOGI(TAG, "Firmware download URL: %s", firmware_url);

            ble_stop_advertising();
            mqtt_manager_stop();
            // stop all timers

            if (temp_reading_timer != NULL)
            {
                xTimerStop(temp_reading_timer, portMAX_DELAY);
                xTimerDelete(temp_reading_timer, portMAX_DELAY);
                temp_reading_timer = NULL;
            }
            if (feeding_timer != NULL)
            {
                xTimerStop(feeding_timer, portMAX_DELAY);
                xTimerDelete(feeding_timer, portMAX_DELAY);
                feeding_timer = NULL;
            }
            if (publish_timer != NULL)
            {
                xTimerStop(publish_timer, portMAX_DELAY);
                xTimerDelete(publish_timer, portMAX_DELAY);
                publish_timer = NULL;
            }
            if (ble_timer != NULL)
            {
                xTimerStop(ble_timer, portMAX_DELAY);
                xTimerDelete(ble_timer, portMAX_DELAY);
                ble_timer = NULL;
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_err_t err = http_manager_perform_ota_update(firmware_url);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(err));
                wifi_manager_stop();
                activity_counter_decrement();
                event_manager_clear_bits(EVENT_BIT_OTA_UPDATE);
                event_manager_set_bits(EVENT_BIT_DEEP_SLEEP);
                continue;
            }

            uint8_t pending_flag = 1;
            esp_err_t nvs_err = nvs_save_blob("firmware", "pending_ota", &pending_flag, sizeof(pending_flag));
            if (nvs_err != ESP_OK)
            {
                ESP_LOGW(TAG, "Failed to save pending OTA flag to NVS: %s", esp_err_to_name(nvs_err));
            }
            else
            {
                ESP_LOGI(TAG, "Saved pending OTA flag to NVS (will be confirmed after verification)");
            }

            wifi_manager_stop();
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        }

        event_manager_set_bits(EVENT_BIT_DEEP_SLEEP);
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

            // Check if all timers are 0 (expired) - if so, don't sleep, let tasks execute
            bool all_timers_expired = true;
            if ((g_temp_reading_interval_sec > 0 && temp_remaining > 0) ||
                (g_feeding_interval_sec > 0 && feed_remaining > 0) ||
                (publish_interval_sec > 0 && publish_remaining > 0) ||
                ble_remaining > 0)
            {
                all_timers_expired = false;
            }

            if (all_timers_expired)
            {
                ESP_LOGI(TAG, "All timers expired (0) - skipping sleep to allow tasks to execute");
                // Don't enter sleep, wait a bit for tasks to execute, then check again
                vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 second
                continue;
            }

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
            uint32_t sleep_duration_sec;
            if (shortest_remaining != UINT32_MAX && shortest_remaining > 0)
            {
                sleep_duration_us = (uint64_t)shortest_remaining * 1000000ULL;
                sleep_duration_sec = shortest_remaining;
                ESP_LOGI(TAG, "Using shortest timer: %lu seconds", (unsigned long)shortest_remaining);
            }
            else
            {
                sleep_duration_us = 60ULL * 60ULL * 1000000ULL; // 1 hour
                sleep_duration_sec = 60 * 60;                   // 1 hour in seconds
                ESP_LOGI(TAG, "No active timers, defaulting to 1 hour (%lld microseconds)",
                         (long long)sleep_duration_us);
            }

            typedef struct
            {
                uint32_t temp_remaining;
                uint32_t feed_remaining;
                uint32_t publish_remaining;
            } timer_remaining_data_t;

            timer_remaining_data_t timer_data = {0};

            // Calculate remaining time after sleep for each timer
            // If timer is configured but expired (0), save full interval so it triggers after sleep
            // If timer has remaining time, subtract sleep duration
            if (g_temp_reading_interval_sec > 0)
            {
                if (temp_remaining == 0)
                {
                    // Timer expired or not active - save full interval so it triggers after sleep
                    timer_data.temp_remaining = g_temp_reading_interval_sec;
                }
                else if (temp_remaining > sleep_duration_sec)
                {
                    timer_data.temp_remaining = temp_remaining - sleep_duration_sec;
                }
                else
                {
                    // Timer will expire during sleep - save 0 to trigger immediately on wake
                    timer_data.temp_remaining = 0;
                }
            }

            if (g_feeding_interval_sec > 0)
            {
                if (feed_remaining == 0)
                {
                    // Timer expired or not active - save full interval so it triggers after sleep
                    timer_data.feed_remaining = g_feeding_interval_sec;
                }
                else if (feed_remaining > sleep_duration_sec)
                {
                    timer_data.feed_remaining = feed_remaining - sleep_duration_sec;
                }
                else
                {
                    // Timer will expire during sleep - save 0 to trigger immediately on wake
                    timer_data.feed_remaining = 0;
                }
            }

            if (publish_interval_sec > 0)
            {
                if (publish_remaining == 0)
                {
                    // Timer expired or not active - save full interval so it triggers after sleep
                    timer_data.publish_remaining = publish_interval_sec;
                }
                else if (publish_remaining > sleep_duration_sec)
                {
                    timer_data.publish_remaining = publish_remaining - sleep_duration_sec;
                }
                else
                {
                    // Timer will expire during sleep - save 0 to trigger immediately on wake
                    timer_data.publish_remaining = 0;
                }
            }

            esp_err_t nvs_err = nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "timer_remaining", &timer_data, sizeof(timer_data));
            if (nvs_err != ESP_OK)
            {
                ESP_LOGW(TAG, "Failed to save timer remaining values before sleep: %s", esp_err_to_name(nvs_err));
            }
            else
            {
                ESP_LOGI(TAG, "Saved timer remaining values to NVS before sleep");
            }

            // Save time sync data before sleep: current synced time + uptime + sleep duration
            if (g_time_synced)
            {
                int64_t current_uptime_us = esp_timer_get_time();
                int64_t elapsed_ms = (current_uptime_us - g_synced_uptime_us) / 1000LL;
                int64_t current_time_ms = g_synced_time_ms + elapsed_ms;
                int64_t time_after_sleep_ms = current_time_ms + (sleep_duration_sec * 1000LL);

                typedef struct
                {
                    int64_t synced_time_ms;
                    int64_t synced_uptime_us;
                } time_sync_data_t;

                time_sync_data_t sync_data = {
                    .synced_time_ms = time_after_sleep_ms,
                    .synced_uptime_us = 0 // Reset to 0 after sleep (new boot)
                };

                nvs_err = nvs_save_blob(EVENT_MANAGER_NVS_NAMESPACE, "time_sync", &sync_data, sizeof(sync_data));
                if (nvs_err != ESP_OK)
                {
                    ESP_LOGW(TAG, "Failed to save time sync data before sleep: %s", esp_err_to_name(nvs_err));
                }
                else
                {
                    ESP_LOGI(TAG, "Saved time sync data before sleep: current_time=%lld ms, sleep_duration=%lu sec, time_after_sleep=%lld ms",
                             (long long)current_time_ms, (unsigned long)sleep_duration_sec, (long long)time_after_sleep_ms);
                }
            }

            esp_sleep_enable_timer_wakeup(sleep_duration_us);
            esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON); // Keep RTC peripherals powered
            esp_sleep_enable_ext0_wakeup(GPIO_CONFIRM_BUTTON, 0);            // 0 = wake on LOW (button pressed)

            vTaskDelay(pdMS_TO_TICKS(500));
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
    initialize_sntp();
    mqtt_manager_init();

    // Load saved time from NVS
    typedef struct
    {
        int64_t synced_time_ms;
        int64_t synced_uptime_us;
    } time_sync_data_t;

    time_sync_data_t sync_data;
    size_t sync_data_size = sizeof(sync_data);
    esp_err_t load_err = nvs_load_blob(EVENT_MANAGER_NVS_NAMESPACE, "time_sync", &sync_data, &sync_data_size);

    if (load_err == ESP_OK && sync_data_size == sizeof(sync_data))
    {
        // Load saved time and calculate current time based on uptime
        g_synced_time_ms = sync_data.synced_time_ms;
        g_synced_uptime_us = sync_data.synced_uptime_us;
        int64_t current_uptime_us = esp_timer_get_time();
        int64_t elapsed_ms = (current_uptime_us - g_synced_uptime_us) / 1000LL;
        int64_t current_time_ms = g_synced_time_ms + elapsed_ms;

        // Set system time from saved time + uptime
        struct timeval tv;
        tv.tv_sec = current_time_ms / 1000LL;
        tv.tv_usec = (current_time_ms % 1000LL) * 1000LL;
        settimeofday(&tv, NULL);

        ESP_LOGI(TAG, "Loaded saved time from NVS: synced_time=%lld ms, synced_uptime=%lld us, current_time=%lld ms",
                 (long long)g_synced_time_ms, (long long)g_synced_uptime_us, (long long)current_time_ms);
        g_time_synced = true;
    }
    else
    {
        ESP_LOGI(TAG, "No saved time in NVS, will sync on first WiFi connection");
        g_time_synced = false;
    }

    // Load thresholds from NVS
    size_t threshold_size = sizeof(float);
    esp_err_t threshold_err;

    threshold_size = sizeof(float);
    threshold_err = nvs_load_blob(EVENT_MANAGER_NVS_NAMESPACE, "temp_lower", &temp_lower, &threshold_size);
    if (threshold_err != ESP_OK || threshold_size != sizeof(float))
    {
        temp_lower = -INFINITY;
        ESP_LOGI(TAG, "No temp_lower threshold in NVS, using default: -INFINITY");
    }

    threshold_size = sizeof(float);
    threshold_err = nvs_load_blob(EVENT_MANAGER_NVS_NAMESPACE, "temp_upper", &temp_upper, &threshold_size);
    if (threshold_err != ESP_OK || threshold_size != sizeof(float))
    {
        temp_upper = INFINITY;
        ESP_LOGI(TAG, "No temp_upper threshold in NVS, using default: INFINITY");
    }

    threshold_size = sizeof(float);
    threshold_err = nvs_load_blob(EVENT_MANAGER_NVS_NAMESPACE, "ph_lower", &ph_lower, &threshold_size);
    if (threshold_err != ESP_OK || threshold_size != sizeof(float))
    {
        ph_lower = -INFINITY;
        ESP_LOGI(TAG, "No ph_lower threshold in NVS, using default: -INFINITY");
    }

    threshold_size = sizeof(float);
    threshold_err = nvs_load_blob(EVENT_MANAGER_NVS_NAMESPACE, "ph_upper", &ph_upper, &threshold_size);
    if (threshold_err != ESP_OK || threshold_size != sizeof(float))
    {
        ph_upper = INFINITY;
        ESP_LOGI(TAG, "No ph_upper threshold in NVS, using default: INFINITY");
    }

    ESP_LOGI(TAG, "Loaded thresholds from NVS: temp=[%.2f, %.2f], ph=[%.2f, %.2f]",
             temp_lower, temp_upper, ph_lower, ph_upper);

    esp_sleep_wakeup_cause_t wake_reason = esp_sleep_get_wakeup_cause();

    if (wake_reason == ESP_SLEEP_WAKEUP_UNDEFINED)
    {
        ESP_LOGI(TAG, "Normal boot (not from deep sleep)");
        hardware_manager_display_wake();
        hardware_manager_display_update();

        // Start WiFi and sync time on normal boot
        ESP_LOGI(TAG, "Starting WiFi and time synchronization on normal boot");
        wifi_manager_start();

        // Wait for WiFi connection, then trigger time sync in connection task
        EventBits_t bits = event_manager_wait_bits(EVENT_BIT_WIFI_STATUS, false, false, pdMS_TO_TICKS(30000));
        if (bits & EVENT_BIT_WIFI_STATUS)
        {
            ESP_LOGI(TAG, "WiFi connected, requesting time synchronization...");
            // Trigger time sync in connection task
            event_manager_set_bits(EVENT_BIT_TIME_SYNC);
            // Wait for time sync to complete
            bits = event_manager_wait_bits(EVENT_BIT_TIME_SYNC, false, false, pdMS_TO_TICKS(30000));
            if (bits & EVENT_BIT_TIME_SYNC)
            {
                ESP_LOGI(TAG, "Time synchronized successfully");
            }
            else
            {
                ESP_LOGW(TAG, "Time synchronization timeout");
            }
        }
        else
        {
            ESP_LOGW(TAG, "WiFi connection timeout on normal boot");
        }

        // Check if OTA confirmation is pending
        uint8_t pending_flag = 0;
        size_t pending_flag_size = sizeof(pending_flag);
        esp_err_t pending_load_err = nvs_load_blob("firmware", "pending_ota", &pending_flag, &pending_flag_size);

        if (pending_load_err == ESP_OK && pending_flag == 1)
        {
            const esp_partition_t *running = esp_ota_get_running_partition();
            if (running != NULL)
            {
                esp_ota_img_states_t ota_state;
                esp_err_t ota_state_err = esp_ota_get_state_partition(running, &ota_state);

                if (ota_state_err == ESP_OK && ota_state == ESP_OTA_IMG_PENDING_VERIFY)
                {
                    ESP_LOGI(TAG, "OTA confirmation pending - marking partition as valid");

                    esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
                    if (ret != ESP_OK)
                    {
                        ESP_LOGE(TAG, "Failed to mark app as valid: %s", esp_err_to_name(ret));
                    }
                    else
                    {
                        ESP_LOGI(TAG, "OTA firmware marked as valid - rollback cancelled");

                        // Clear the pending flag
                        pending_flag = 0;
                        nvs_save_blob("firmware", "pending_ota", &pending_flag, sizeof(pending_flag));

                        mqtt_manager_enqueue_log("firmware_update", "success");
                        ESP_LOGI(TAG, "Enqueued firmware update confirmation");
                        event_manager_set_bits(EVENT_BIT_PUBLISH_SCHEDULED);
                    }
                }
            }
        }
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