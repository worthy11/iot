#include "event_manager.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>
#include <math.h>

#include "ble/ble_manager.h"
#include "ble/device_provisioning_service.h"
#include "wifi/wifi_manager.h"
#include "mqtt/mqtt_manager.h"
#include "hardware/hardware_manager.h"
#include "data/aquarium_data.h"
#include "utils/nvs_utils.h"
#include "esp_mac.h"

#define GATT_SERVER_TIMEOUT_MS (5 * 60 * 1000)
#define PH_CONFIRMATION_TIMEOUT_MS (30 * 1000)
#define CONNECTION_TIMEOUT_MS (30 * 1000)

static const char *TAG = "event_manager";
static EventGroupHandle_t s_event_group = NULL;
static const int MAX_FEED_ATTEMPTS = 5;

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
    if (num_notifications < MAX_NOTIFICATIONS)
    {
        notification_registrations[num_notifications].task_handle = task_handle;
        notification_registrations[num_notifications].event_bits = bits;
        num_notifications++;
        ESP_LOGI(TAG, "Registered notification for task 0x%p, events: 0x%lx", task_handle, bits);
    }
    else
    {
        ESP_LOGW(TAG, "Maximum number of notification registrations reached");
    }
}

void event_manager_get_aquarium_data(aquarium_data_t *data)
{
    aquarium_data_get(data);
}

uint32_t event_manager_get_passkey(void)
{
    return ble_manager_get_passkey();
}

// BLE task
void event_manager_ble_task(void *pvParameters)
{
    (void)pvParameters;
    TaskHandle_t task_handle = xTaskGetCurrentTaskHandle();
    event_manager_register_notification(task_handle, EVENT_BIT_CONFIG_MODE | EVENT_BIT_PASSKEY_DISPLAY);

    while (1)
    {
        uint32_t notif = 0;
        bool config_mode = false;

        while (1)
        {
            EventBits_t bits = event_manager_get_bits();

            if (bits & EVENT_BIT_CONFIG_MODE)
            {
                if (!config_mode)
                {
                    ble_start_advertising();
                    ESP_LOGI(TAG, "Advertising started");
                    config_mode = true;
                }

                hardware_manager_display_interrupt();
                xTaskNotifyWait(0, UINT32_MAX, &notif, pdMS_TO_TICKS(GATT_SERVER_TIMEOUT_MS));

                if (notif)
                {
                    bits = event_manager_get_bits();
                    if (bits & EVENT_BIT_PASSKEY_DISPLAY) // PASSKEY_DISPLAY set
                    {
                        hardware_manager_display_interrupt();
                        xTaskNotifyWait(0, UINT32_MAX, &notif, pdMS_TO_TICKS(GATT_SERVER_TIMEOUT_MS));
                        if (!notif) // PASSKEY_DISPLAY not cleared
                        {
                            ESP_LOGI(TAG, "Passkey timeout");
                            event_manager_clear_bits(EVENT_BIT_CONFIG_MODE);
                            ble_stop_advertising();
                            hardware_manager_display_update();
                        }
                        notif = 0;
                        continue;
                    }
                    else // CONFIG_MODE cleared
                    {
                        ESP_LOGI(TAG, "Config mode off");
                        ble_stop_advertising();
                        notif = 0;
                        continue;
                    }
                }
                else // CONFIG_MODE not cleared
                {
                    ESP_LOGI(TAG, "Config mode timeout");
                    event_manager_clear_bits(EVENT_BIT_CONFIG_MODE);
                    ble_stop_advertising();
                    hardware_manager_display_update();
                    notif = 0;
                    continue;
                }
            }

            else // No notification
            {
                xTaskNotifyWait(0, UINT32_MAX, &notif, portMAX_DELAY);
                config_mode = false;
                notif = 0;
                continue;
            }
        }
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
            ESP_LOGI(TAG, "Display previous");
            hardware_manager_display_prev();
        }
        else if (bits & EVENT_BIT_DISPLAY_CONFIRM)
        {
            ESP_LOGI(TAG, "Display confirm");
            hardware_manager_display_confirm();
        }
    }
}

// Measurement task
static void event_manager_measurement_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Sensor measurement task started");
    uint32_t notif = 0;
    TaskHandle_t task_handle = xTaskGetCurrentTaskHandle();
    event_manager_register_notification(task_handle, EVENT_BIT_PH_CONFIRMED);

    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(
            EVENT_BIT_TEMP_SCHEDULED | EVENT_BIT_PH_SCHEDULED,
            false,
            false,
            portMAX_DELAY);

        if (bits & EVENT_BIT_TEMP_SCHEDULED)
        {
            hardware_manager_display_interrupt();
            float temp = hardware_manager_measure_temp();
            event_manager_clear_bits(EVENT_BIT_TEMP_SCHEDULED);
            if (!isnan(temp))
            {
                aquarium_data_update_temperature(temp);
                event_manager_set_bits(EVENT_BIT_TEMP_UPDATED);
                // Display result for 1 second
                hardware_manager_display_interrupt_with_value(temp, true);
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }

        if (bits & EVENT_BIT_PH_SCHEDULED)
        {
            hardware_manager_display_interrupt();
            xTaskNotifyWait(0, UINT32_MAX, &notif, pdMS_TO_TICKS(PH_CONFIRMATION_TIMEOUT_MS));
            if (!notif) // PH_CONFIRMED not set
            {
                ESP_LOGI(TAG, "pH confirmation timeout");
                event_manager_clear_bits(EVENT_BIT_PH_SCHEDULED);
                continue;
            }
            else
            {
                ESP_LOGI(TAG, "pH confirmed");
                hardware_manager_display_interrupt();
            }
            float ph_value = hardware_manager_measure_ph();
            event_manager_clear_bits(EVENT_BIT_PH_SCHEDULED);
            event_manager_clear_bits(EVENT_BIT_PH_CONFIRMED);
            if (!isnan(ph_value))
            {
                aquarium_data_update_ph(ph_value);
                event_manager_set_bits(EVENT_BIT_PH_UPDATED);
                hardware_manager_display_interrupt_with_value(ph_value, false);
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
        hardware_manager_display_update();
    }
}

// Feeding task
void event_manager_feeding_task(void *pvParameters)
{
    (void)pvParameters;
    ESP_LOGI(TAG, "Feed coordinator task started");

    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(
            EVENT_BIT_FEED_SCHEDULED,
            true,  // Clear on exit
            false, // Wait for any
            portMAX_DELAY);

        if (bits & EVENT_BIT_FEED_SCHEDULED)
        {
            TaskHandle_t beam_task_handle = NULL;
            hardware_manager_start_beam_monitor(&beam_task_handle);

            bool feed_successful = false;
            for (int attempt = 1; attempt <= MAX_FEED_ATTEMPTS; attempt++)
            {
                ESP_LOGI(TAG, "Feeding attempt %d/%d", attempt, MAX_FEED_ATTEMPTS);

                if (attempt > 1)
                {
                    hardware_manager_motor_rotate_portion(false);
                    vTaskDelay(pdMS_TO_TICKS(GPIO_MOTOR_RETRY_DELAY_MS));
                }

                hardware_manager_motor_rotate_portion(true);
                vTaskDelay(pdMS_TO_TICKS(GPIO_MOTOR_RETRY_DELAY_MS));

                if (beam_task_handle == NULL)
                {
                    feed_successful = true;
                    ESP_LOGI(TAG, "Beam break detected on attempt %d", attempt);
                    break;
                }
            }

            if (beam_task_handle != NULL)
            {
                hardware_manager_stop_beam_monitor(beam_task_handle);
                beam_task_handle = NULL;
            }

            time_t feed_time = time(NULL);
            if (feed_time > 1000000000)
            {
                aquarium_data_update_last_feed(feed_time, feed_successful);
                ESP_LOGI(TAG, "Feed time saved: %ld", (long)feed_time);
            }
            else
            {
                ESP_LOGW(TAG, "System time not synchronized, feed time not saved (time=%ld)", (long)feed_time);
            }

            uint32_t feeding_interval = aquarium_data_get_feeding_interval();
            if (feeding_interval > 0)
            {
                time_t next_feed_time = feed_time + feeding_interval;
                aquarium_data_update_next_feed(next_feed_time);
            }

            event_manager_set_bits(EVENT_BIT_FEED_UPDATED);
            hardware_manager_display_update();

            if (feed_successful)
            {
                ESP_LOGI(TAG, "Feed successful, updating last feed time");
            }
            else
            {
                ESP_LOGW(TAG, "Feed failed - no beam break detected after %d attempts", MAX_FEED_ATTEMPTS);
            }
        }
    }
}

// Rescheduling task
void event_manager_scheduling_task(void *pvParameters)
{
    (void)pvParameters;
    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(EVENT_BIT_TEMP_RESCHEDULED | EVENT_BIT_FEED_RESCHEDULED, true, false, portMAX_DELAY);
        if (bits & EVENT_BIT_TEMP_RESCHEDULED)
        {
            int temp_frequency = mqtt_manager_get_temp_frequency();
            aquarium_data_set_temp_reading_interval(temp_frequency);
            hardware_manager_set_temp_reading_interval(temp_frequency);
        }
        if (bits & EVENT_BIT_FEED_RESCHEDULED)
        {
            int feed_frequency = mqtt_manager_get_feed_frequency();
            aquarium_data_set_feeding_interval(feed_frequency);
            hardware_manager_set_feeding_interval(feed_frequency);
            event_manager_set_bits(EVENT_BIT_FEED_SCHEDULED);
        }
    }
}

// Publish task
void event_manager_publish_task(void *pvParameters)
{
    (void)pvParameters;

    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(
            EVENT_BIT_TEMP_UPDATED | EVENT_BIT_PH_UPDATED | EVENT_BIT_FEED_UPDATED | EVENT_BIT_PROVISION_TRIGGER,
            true,
            false,
            portMAX_DELAY);

        esp_err_t err = mqtt_manager_start();
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to start MQTT client: %s. Cannot enqueue.", esp_err_to_name(err));
            continue;
        }
        else
        {
            ESP_LOGI(TAG, "MQTT client started");
        }

        aquarium_data_t data;
        event_manager_get_aquarium_data(&data);
        char message[64];
        time_t timestamp = time(NULL);

        if (bits & EVENT_BIT_TEMP_UPDATED)
        {
            snprintf(message, sizeof(message), "%.2f,%ld", data.temperature, (long)timestamp);
            mqtt_manager_enqueue_data("temperature", message);
            ESP_LOGI(TAG, "Enqueued temperature: %s", message);
        }

        if (bits & EVENT_BIT_PH_UPDATED)
        {
            snprintf(message, sizeof(message), "%.2f,%ld", data.ph, (long)timestamp);
            mqtt_manager_enqueue_data("ph", message);
            ESP_LOGI(TAG, "Enqueued pH: %s", message);
        }

        if (bits & EVENT_BIT_FEED_UPDATED)
        {
            const char *status = data.last_feed_success ? "success" : "failure";
            snprintf(message, sizeof(message), "%ld,%s", (long)data.last_feed_time, status);
            mqtt_manager_enqueue_data("feed", message);
            ESP_LOGI(TAG, "Enqueued feed: %s", message);
        }

        if (bits & EVENT_BIT_PROVISION_TRIGGER)
        {
            ESP_LOGI(TAG, "Provisioning trigger received, reloading configurations");

            esp_err_t err = wifi_manager_load_config();
            if (err != ESP_OK)
            {
                ESP_LOGW(TAG, "Failed to reload WiFi config: %s", esp_err_to_name(err));
            }

            err = mqtt_manager_load_config();
            if (err != ESP_OK)
            {
                ESP_LOGW(TAG, "Failed to reload MQTT config: %s", esp_err_to_name(err));
            }
            else
            {
                ESP_LOGI(TAG, "MQTT configuration reloaded successfully");
                mqtt_manager_enqueue_data("logs", "{\"status\":\"provisioned\"}");
            }
        }

        err = wifi_manager_start();
        bits = event_manager_wait_bits(EVENT_BIT_WIFI_STATUS, true, false, pdMS_TO_TICKS(CONNECTION_TIMEOUT_MS));
        if (!(bits & EVENT_BIT_WIFI_STATUS))
        {
            ESP_LOGW(TAG, "WiFi connection timeout");
            wifi_manager_stop();
            continue;
        }

        bits = event_manager_wait_bits(EVENT_BIT_MQTT_STATUS, true, false, pdMS_TO_TICKS(CONNECTION_TIMEOUT_MS));
        if (!(bits & EVENT_BIT_MQTT_STATUS))
        {
            ESP_LOGW(TAG, "MQTT connection timeout");
            mqtt_manager_stop();
            wifi_manager_stop();
            continue;
        }

        ESP_LOGI(TAG, "Data sent");
        mqtt_manager_stop();
        wifi_manager_stop();
    }
}

void event_manager_init(void)
{
    s_event_group = xEventGroupCreate();
    num_notifications = 0;

    aquarium_data_init();
    wifi_manager_init();
    ble_manager_init();
    mqtt_manager_init();
    hardware_manager_init();

    xTaskCreate(
        event_manager_ble_task,
        "ble_coordinator",
        4096,
        NULL,
        2,
        NULL);

    xTaskCreate(
        event_manager_display_task,
        "display_coordinator",
        2048,
        NULL,
        3,
        NULL);

    xTaskCreate(
        event_manager_measurement_task,
        "measurement_coordinator",
        4096,
        NULL,
        3,
        NULL);

    xTaskCreate(
        event_manager_feeding_task,
        "feeding_coordinator",
        2048,
        NULL,
        4,
        NULL);

    xTaskCreate(
        event_manager_publish_task,
        "publish_coordinator",
        4096,
        NULL,
        5,
        NULL);

    ESP_LOGI(TAG, "Event manager initialized");
}