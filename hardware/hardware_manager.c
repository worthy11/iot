#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include <time.h>
#include <math.h>
#include <string.h>

#include "hardware_manager.h"
#include "event_manager.h"
#include "ph/ph_sensor_driver.h"
#include "feeder/motor_driver.h"
#include "feeder/beam_driver.h"
#include "display/display_driver.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NUM_READINGS 5
#define TEMP_INTERVAL_MS 5 * 1000
#define PH_INTERVAL_MS 2 * 1000
#define MAX_FEED_ATTEMPTS 5

static const char *TAG = "hardware_manager";

void hardware_manager_display_event(const char *event, float value)
{
    display_event(event, value);
}

void hardware_manager_display_update(void)
{
    display_update();
}

void hardware_manager_display_wake(void)
{
    display_wake();
}

void hardware_manager_display_next(void)
{
    display_next();
}

void hardware_manager_display_prev(void)
{
    display_prev();
}

void hardware_manager_display_confirm(void)
{
    display_confirm();
}

void hardware_manager_set_temp_reading_interval(uint32_t interval_seconds)
{
    event_manager_set_temp_reading_interval(interval_seconds);
}

void hardware_manager_set_feeding_interval(uint32_t interval_seconds)
{
    event_manager_set_feeding_interval(interval_seconds);
}

float hardware_manager_measure_temp(void)
{
    float temp_sum = 0.0f;
    int valid_readings = 0;

    for (int i = 0; i < NUM_READINGS; i++)
    {
        float temp = temp_sensor_read();
        if (!isnan(temp))
        {
            ESP_LOGI(TAG, "Temperature reading %d: %.2f°C", i + 1, temp);
            if (temp > 40.0f || temp < 10.0f)
            {
                ESP_LOGW(TAG, "Temperature reading %d out of range (%.2f°C)", i + 1, temp);
            }
            else
            {
                temp_sum += temp;
                valid_readings++;
            }
        }
        else
        {
            ESP_LOGW(TAG, "Temperature reading %d failed (NaN)", i + 1);
        }

        if (i < NUM_READINGS - 1)
        {
            esp_sleep_enable_timer_wakeup(TEMP_INTERVAL_MS * 1000);
            ESP_LOGI(TAG, "Entering light sleep for %d milliseconds", TEMP_INTERVAL_MS);
            esp_light_sleep_start();
            ESP_LOGI(TAG, "Exited light sleep");
        }
    }

    if (valid_readings > 0)
    {
        float temp = temp_sum / valid_readings;
        display_set_temperature(temp);
        hardware_manager_display_event("temperature", temp);
        return temp;
    }
    else
    {
        hardware_manager_display_event("temperature", NAN);
        ESP_LOGE(TAG, "All temperature readings failed");
        return NAN;
    }
}

float hardware_manager_measure_ph(void)
{
    gpio_set_level(GPIO_PH_POWER, 1);
    vTaskDelay(pdMS_TO_TICKS(PH_POWER_STABILIZE_MS));

    float ph_sum = 0.0f;
    int valid_readings = 0;

    for (int i = 0; i < NUM_READINGS; i++)
    {
        float ph_value = ph_sensor_read_ph();
        if (!isnan(ph_value))
        {
            ESP_LOGI(TAG, "pH reading %d: %.2f", i + 1, ph_value);
            ph_sum += ph_value;
            valid_readings++;
        }
        else
        {
            ESP_LOGW(TAG, "pH reading %d failed (NaN)", i + 1);
        }

        if (i < NUM_READINGS - 1)
        {
            esp_sleep_enable_timer_wakeup(PH_INTERVAL_MS * 1000);
            ESP_LOGI(TAG, "Entering light sleep for %d milliseconds", PH_INTERVAL_MS);
            esp_light_sleep_start();
            ESP_LOGI(TAG, "Exited light sleep");
        }
    }

    gpio_set_level(GPIO_PH_POWER, 0);

    if (valid_readings > 0)
    {
        float ph = ph_sum / valid_readings;
        display_set_ph(ph);
        hardware_manager_display_event("ph", ph);
        return ph;
    }
    else
    {
        ESP_LOGE(TAG, "All pH readings failed");
        hardware_manager_display_event("ph", NAN);
        return NAN;
    }
}

bool hardware_manager_feed(void)
{
    // Power on the break beam sensor before starting the task
    break_beam_power_on();

    TaskHandle_t beam_task_handle = NULL;
    xTaskCreate(
        break_beam_monitor,
        "beam_monitor",
        2048,
        &beam_task_handle,
        5,
        &beam_task_handle);

    bool feed_successful = false;
    for (int attempt = 1; attempt <= MAX_FEED_ATTEMPTS; attempt++)
    {
        if (attempt > 1)
        {
            motor_rotate_portion(false);
            vTaskDelay(pdMS_TO_TICKS(GPIO_MOTOR_RETRY_DELAY_MS));
        }

        motor_rotate_portion(true);
        vTaskDelay(pdMS_TO_TICKS(GPIO_MOTOR_RETRY_DELAY_MS));

        if (beam_task_handle == NULL)
        {
            feed_successful = true;
            break;
        }
    }

    if (beam_task_handle != NULL)
    {
        // Power off the break beam sensor before deleting the task
        break_beam_power_off();
        vTaskDelete(beam_task_handle);
        beam_task_handle = NULL;
    }

    if (feed_successful)
    {
        ESP_LOGI(TAG, "Feed successful");
        time_t feed_time = time(NULL);
        display_set_feed_time(feed_time);
        hardware_manager_display_event("feed_status", 1.0f);
    }
    else
    {
        ESP_LOGW(TAG, "Feed failed after %d attempts", MAX_FEED_ATTEMPTS);
        hardware_manager_display_event("feed_status", 0.0f);
    }

    return feed_successful;
}

void hardware_manager_init(void)
{
    display_init(GPIO_OLED_SCL, GPIO_OLED_SDA);
    left_button_init(GPIO_LEFT_BUTTON);
    right_button_init(GPIO_RIGHT_BUTTON);
    confirm_button_init(GPIO_CONFIRM_BUTTON);
    break_beam_init(GPIO_BREAK_BEAM, GPIO_BREAK_BEAM_POWER);
    motor_driver_init(GPIO_MOTOR_IN1, GPIO_MOTOR_IN2, GPIO_MOTOR_IN3, GPIO_MOTOR_IN4);
    ph_sensor_init(GPIO_PH_OUTPUT, GPIO_PH_TEMP_COMP);
    temp_sensor_init(GPIO_TEMP_SENSOR);

    gpio_config_t ph_power_cfg = {
        .pin_bit_mask = (1ULL << GPIO_PH_POWER),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&ph_power_cfg);
    gpio_set_level(GPIO_PH_POWER, 0);

    ESP_LOGI(TAG, "Hardware manager initialized");
}