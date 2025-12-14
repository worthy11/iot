#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include <time.h>

#include "hardware_manager.h"
#include "wifi_manager.h"
#include "event_manager.h"
#include "aquarium_data.h"
#include "ph/ph_sensor_driver.h"
#include "feeder/motor_driver.h"
#include "feeder/beam_driver.h"

static const char *TAG = "hardware_manager";
TaskHandle_t led_task_handle = NULL;

static EventGroupHandle_t s_hardware_event_group = NULL;
static TaskHandle_t beam_task_handle = NULL;
static const int MAX_FEED_ATTEMPTS = 5;
static const uint32_t BEAM_MONITOR_TIMEOUT_MS = 15000;

static void beam_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Beam monitor task started");
    bool success = break_beam_monitor(BEAM_MONITOR_TIMEOUT_MS);

    // Set success or failure bit in hardware manager event group
    if (s_hardware_event_group != NULL)
    {
        if (success)
        {
            xEventGroupSetBits(s_hardware_event_group, HARDWARE_BIT_FEED_SUCCESS);
            ESP_LOGI(TAG, "Feed success bit set");
        }
        else
        {
            xEventGroupSetBits(s_hardware_event_group, HARDWARE_BIT_FEED_FAILURE);
            ESP_LOGI(TAG, "Feed failure bit set");
        }
    }

    beam_task_handle = NULL; // Clear handle before deleting
    vTaskDelete(NULL);
}

static void feed_coordinator_task(void *pvParameters)
{
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
            ESP_LOGI(TAG, "Feed requested, starting feeding sequence");

            xEventGroupClearBits(s_hardware_event_group, HARDWARE_BIT_FEED_SUCCESS | HARDWARE_BIT_FEED_FAILURE);

            TaskHandle_t local_beam_task_handle = NULL;
            xTaskCreate(
                beam_monitor_task,
                "beam_monitor",
                2048,
                NULL,
                5,
                &local_beam_task_handle);
            beam_task_handle = local_beam_task_handle;

            vTaskDelay(pdMS_TO_TICKS(100));

            bool feed_successful = false;

            for (int attempt = 1; attempt <= MAX_FEED_ATTEMPTS; attempt++)
            {
                ESP_LOGI(TAG, "Feeding attempt %d/%d", attempt, MAX_FEED_ATTEMPTS);

                motor_rotate_portion();
                vTaskDelay(pdMS_TO_TICKS(500));

                EventBits_t hardware_bits = xEventGroupGetBits(s_hardware_event_group);
                if (hardware_bits & HARDWARE_BIT_FEED_SUCCESS)
                {
                    ESP_LOGI(TAG, "Food detected on attempt %d", attempt);
                    feed_successful = true;
                    break;
                }
            }

            TaskHandle_t task_to_delete = beam_task_handle;
            if (task_to_delete != NULL)
            {
                vTaskDelete(task_to_delete);
                beam_task_handle = NULL;
            }

            EventBits_t hardware_bits = xEventGroupGetBits(s_hardware_event_group);
            xEventGroupClearBits(s_hardware_event_group, HARDWARE_BIT_FEED_SUCCESS | HARDWARE_BIT_FEED_FAILURE);

            if (feed_successful)
            {
                ESP_LOGI(TAG, "Feed successful, updating last feed time");
                aquarium_data_update_last_feed(time(NULL));
            }
            else
            {
                ESP_LOGW(TAG, "Feed failed - no beam break detected after %d attempts", MAX_FEED_ATTEMPTS);
            }
        }
    }
}

static void sensor_measurement_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Sensor measurement task started");

    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(
            EVENT_BIT_MEASURE_TEMP | EVENT_BIT_MEASURE_PH,
            true,  // Clear on exit
            false, // Wait for any
            portMAX_DELAY);

        if (bits & EVENT_BIT_MEASURE_TEMP)
        {
            ESP_LOGI(TAG, "Measuring temperature on request");
            // TODO: Read temperature sensor when implemented
            // float temp = temp_sensor_read();
            // aquarium_data_update_temperature(temp);
            ESP_LOGW(TAG, "Temperature sensor not yet implemented");
        }

        if (bits & EVENT_BIT_MEASURE_PH)
        {
            ESP_LOGI(TAG, "Measuring pH on request");
            float ph_value = ph_sensor_read_ph();
            aquarium_data_update_ph(ph_value);
            ESP_LOGI(TAG, "pH measurement complete: %.2f", ph_value);
        }
    }
}

static void led_task(void *pvParameters)
{
    ESP_LOGI(TAG, "LED task started");
    bool led_state = false;
    const TickType_t flash_period = pdMS_TO_TICKS(500); // 500ms flash period
    TaskHandle_t task_handle = xTaskGetCurrentTaskHandle();

    event_manager_register_notification(task_handle, EVENT_BIT_WIFI_STATUS);

    EventBits_t bits = event_manager_get_bits();
    bool wifi_connected = (bits & EVENT_BIT_WIFI_STATUS) != 0;

    while (1)
    {
        if (!wifi_connected)
        {
            led_state = !led_state;
            gpio_set_level(GPIO_LED, led_state);

            uint32_t notification_value;
            if (xTaskNotifyWait(0, ULONG_MAX, &notification_value, flash_period) == pdTRUE)
            {
                bits = event_manager_get_bits();
                wifi_connected = (bits & EVENT_BIT_WIFI_STATUS) != 0;
            }
        }
        else
        {
            gpio_set_level(GPIO_LED, 0);
            led_state = false;

            uint32_t notification_value;
            if (xTaskNotifyWait(0, ULONG_MAX, &notification_value, portMAX_DELAY) == pdTRUE)
            {
                bits = event_manager_get_bits();
                wifi_connected = (bits & EVENT_BIT_WIFI_STATUS) != 0;
            }
        }
    }
}

EventGroupHandle_t hardware_manager_get_event_group(void)
{
    return s_hardware_event_group;
}

void hardware_init(void)
{
    // Create hardware manager event group
    s_hardware_event_group = xEventGroupCreate();
    if (s_hardware_event_group == NULL)
    {
        ESP_LOGE(TAG, "Failed to create hardware event group");
        return;
    }

    aquarium_data_init();
    display_init(GPIO_OLED_SCL, GPIO_OLED_SDA);
    left_button_init(GPIO_LEFT_BUTTON);
    right_button_init(GPIO_RIGHT_BUTTON);
    confirm_button_init(GPIO_CONFIRM_BUTTON);
    break_beam_init(GPIO_BREAK_BEAM);
    motor_driver_init(GPIO_MOTOR_IN1, GPIO_MOTOR_IN2, GPIO_MOTOR_IN3, GPIO_MOTOR_IN4);
    ph_sensor_init(GPIO_PH_OUTPUT, GPIO_PH_TEMP_COMP);
    temp_sensor_init(GPIO_TEMP_SENSOR);

    // Initialize LED GPIO
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << GPIO_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&led_conf);
    gpio_set_level(GPIO_LED, 0); // Start with LED off

    xTaskCreate(
        feed_coordinator_task,
        "feed_coordinator",
        2048,
        NULL,
        4,
        NULL);

    xTaskCreate(
        sensor_measurement_task,
        "sensor_measurement",
        4096,
        NULL,
        3,
        NULL);

    xTaskCreate(
        led_task,
        "led_task",
        2048,
        NULL,
        2,
        &led_task_handle);

    ESP_LOGI(TAG, "Hardware manager initialized");
}