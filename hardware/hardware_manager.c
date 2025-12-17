#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include <time.h>
#include <math.h>

#include "hardware_manager.h"
#include "event_manager.h"
#include "aquarium_data.h"
#include "ph/ph_sensor_driver.h"
#include "feeder/motor_driver.h"
#include "feeder/beam_driver.h"
#include "mqtt_publisher.h"

static const char *TAG = "hardware_manager";
TaskHandle_t led_task_handle = NULL;

static EventGroupHandle_t s_hardware_event_group = NULL;
static TaskHandle_t beam_task_handle = NULL;
static const int MAX_FEED_ATTEMPTS = 5;

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

            xTaskCreate(
                break_beam_monitor,
                "beam_monitor",
                2048,
                &beam_task_handle,
                5,
                &beam_task_handle);
            vTaskDelay(pdMS_TO_TICKS(100));

            bool feed_successful = false;
            for (int attempt = 1; attempt <= MAX_FEED_ATTEMPTS; attempt++)
            {
                ESP_LOGI(TAG, "Feeding attempt %d/%d", attempt, MAX_FEED_ATTEMPTS);

                if (attempt > 1)
                {
                    motor_rotate_portion(false);
                    vTaskDelay(pdMS_TO_TICKS(GPIO_MOTOR_RETRY_DELAY_MS));
                }

                motor_rotate_portion(true);
                vTaskDelay(pdMS_TO_TICKS(GPIO_MOTOR_RETRY_DELAY_MS));

                if (beam_task_handle == NULL)
                {
                    ESP_LOGI(TAG, "Food detected on attempt %d", attempt);
                    feed_successful = true;
                    break;
                }
            }

            if (beam_task_handle != NULL)
            {
                vTaskDelete(beam_task_handle);
                beam_task_handle = NULL;
            }

            time_t feed_time = time(NULL);
            aquarium_data_update_last_feed(feed_time, feed_successful);
            event_manager_set_bits(EVENT_BIT_FEED_UPDATED);

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

static void sensor_measurement_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Sensor measurement task started");

    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(
            EVENT_BIT_MEASURE_TEMP | EVENT_BIT_MEASURE_PH,
            true,
            false,
            portMAX_DELAY);

        if (bits & EVENT_BIT_MEASURE_TEMP)
        {
            ESP_LOGI(TAG, "Measuring temperature on request");
            float temp_sum = 0.0f;
            int valid_readings = 0;

            for (int i = 0; i < 3; i++)
            {
                float temp = temp_sensor_read();
                if (!isnan(temp))
                {
                    temp_sum += temp;
                    valid_readings++;
                }
            }

            if (valid_readings > 0)
            {
                float avg_temp = temp_sum / valid_readings;
                aquarium_data_update_temperature(avg_temp);
                event_manager_set_bits(EVENT_BIT_TEMP_UPDATED);
            }
            else
            {
                ESP_LOGE(TAG, "All temperature readings failed");
            }
        }

        if (bits & EVENT_BIT_MEASURE_PH)
        {
            ESP_LOGI(TAG, "pH measurement requested - waiting for user confirmation");

            // Wait for confirmation from display_manager (with timeout)
            EventBits_t confirm_bits = event_manager_wait_bits(
                EVENT_BIT_PH_MEASUREMENT_CONFIRMED,
                true,                  // Clear on exit
                false,                 // Wait for any
                pdMS_TO_TICKS(61000)); // Slightly longer than display timeout

            if (confirm_bits & EVENT_BIT_PH_MEASUREMENT_CONFIRMED)
            {
                ESP_LOGI(TAG, "pH measurement confirmed - taking reading");
                float ph_value = ph_sensor_read_ph();
                aquarium_data_update_ph(ph_value);
                event_manager_set_bits(EVENT_BIT_PH_UPDATED);
            }
            else
            {
                ESP_LOGI(TAG, "pH measurement cancelled or timed out");
            }
        }
    }
}

static void temp_reading_scheduler_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Temperature reading scheduler task started");
    TaskHandle_t task_handle = xTaskGetCurrentTaskHandle();

    event_manager_register_notification(task_handle, EVENT_BIT_TEMP_INTERVAL_CHANGED);

    uint32_t current_interval = aquarium_data_get_temp_reading_interval();
    if (current_interval == 0)
    {
        ESP_LOGI(TAG, "Temperature reading disabled");
    }

    while (1)
    {
        if (current_interval > 0)
        {
            while (1)
            {
                time_t now = time(NULL);
                time_t last_measurement = aquarium_data_get_last_temp_measurement_time();
                time_t elapsed = now - last_measurement;

                uint32_t wait_time_ms;
                if (elapsed >= current_interval)
                {
                    event_manager_set_bits(EVENT_BIT_MEASURE_TEMP);
                    wait_time_ms = current_interval * 1000;
                }
                else
                {
                    wait_time_ms = (current_interval - elapsed) * 1000;
                }

                uint32_t notification_value;
                if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_time_ms)) > 0)
                {
                    break;
                }
            }
        }
        else
        {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }

        uint32_t new_interval = aquarium_data_get_temp_reading_interval();
        if (new_interval == 0)
        {
            ESP_LOGI(TAG, "Temperature reading disabled");
            current_interval = 0;
        }
        else
        {
            ESP_LOGI(TAG, "Temperature reading interval changed to %lu seconds", new_interval);
            current_interval = new_interval;
        }
    }
}

static void feeding_scheduler_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Feeding scheduler task started");
    TaskHandle_t task_handle = xTaskGetCurrentTaskHandle();

    event_manager_register_notification(task_handle, EVENT_BIT_FEED_INTERVAL_CHANGED);

    uint32_t current_interval = aquarium_data_get_feeding_interval();
    if (current_interval == 0)
    {
        ESP_LOGI(TAG, "Feeding disabled");
    }

    while (1)
    {
        if (current_interval > 0)
        {
            while (1)
            {
                time_t now = time(NULL);
                time_t last_feed = aquarium_data_get_last_feed_time();
                time_t elapsed = now - last_feed;

                uint32_t wait_time_ms;
                if (elapsed >= current_interval)
                {
                    event_manager_set_bits(EVENT_BIT_FEED_SCHEDULED);
                    wait_time_ms = current_interval * 1000;
                }
                else
                {
                    wait_time_ms = (current_interval - elapsed) * 1000;
                }

                uint32_t notification_value;
                if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_time_ms)) > 0)
                {
                    break;
                }
            }
        }
        else
        {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }

        uint32_t new_interval = aquarium_data_get_feeding_interval();
        if (new_interval == 0)
        {
            ESP_LOGI(TAG, "Feeding disabled (interval set to 0)");
            current_interval = 0;
        }
        else
        {
            ESP_LOGI(TAG, "Feeding interval changed to %lu seconds", new_interval);
            current_interval = new_interval;
        }
    }
}

void hardware_init(void)
{
    s_hardware_event_group = xEventGroupCreate();

    aquarium_data_init();
    display_init(GPIO_OLED_SCL, GPIO_OLED_SDA);
    left_button_init(GPIO_LEFT_BUTTON);
    right_button_init(GPIO_RIGHT_BUTTON);
    confirm_button_init(GPIO_CONFIRM_BUTTON);
    break_beam_init(GPIO_BREAK_BEAM);
    motor_driver_init(GPIO_MOTOR_IN1, GPIO_MOTOR_IN2, GPIO_MOTOR_IN3, GPIO_MOTOR_IN4);
    ph_sensor_init(GPIO_PH_OUTPUT, GPIO_PH_TEMP_COMP);
    temp_sensor_init(GPIO_TEMP_SENSOR);

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
        temp_reading_scheduler_task,
        "temp_scheduler",
        2048,
        NULL,
        3,
        NULL);

    xTaskCreate(
        feeding_scheduler_task,
        "feeding_scheduler",
        2048,
        NULL,
        3,
        NULL);

    ESP_LOGI(TAG, "Hardware manager initialized");
}

EventGroupHandle_t hardware_manager_get_event_group(void)
{
    return s_hardware_event_group;
}