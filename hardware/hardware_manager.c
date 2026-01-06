#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include <time.h>
#include <math.h>

#include "hardware_manager.h"
#include "event_manager.h"
#include "ph/ph_sensor_driver.h"
#include "feeder/motor_driver.h"
#include "feeder/beam_driver.h"
#include "display/display_driver.h"

static const char *TAG = "hardware_manager";
TaskHandle_t led_task_handle = NULL;

static EventGroupHandle_t s_hardware_event_group = NULL;
static TimerHandle_t temp_reading_timer = NULL;
static TimerHandle_t feeding_timer = NULL;

static void temp_reading_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "Temperature reading timer expired");
    event_manager_set_bits(EVENT_BIT_TEMP_SCHEDULED);
}

static void feeding_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "Feeding timer expired");
    event_manager_set_bits(EVENT_BIT_FEED_SCHEDULED);
}

void hardware_manager_display_interrupt(void)
{
    display_interrupt();
}

void hardware_manager_display_interrupt_with_value(float value, bool is_temp)
{
    display_interrupt_with_value(value, is_temp);
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

float hardware_manager_measure_temp(void)
{
    float temp_sum = 0.0f;
    int valid_readings = 0;

    for (int i = 0; i < 5; i++)
    {
        float temp = temp_sensor_read();
        if (!isnan(temp))
        {
            ESP_LOGI(TAG, "Temperature reading %d: %.2f°C", i + 1, temp);
            if (temp > 40.0f || temp < 10.0f)
            {
                ESP_LOGW(TAG, "Temperature reading %d out of range (%.2f°C)", i + 1, temp);
                continue;
            }
            temp_sum += temp;
            valid_readings++;
        }
        else
        {
            ESP_LOGW(TAG, "Temperature reading %d failed (NaN)", i + 1);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (valid_readings > 0)
    {
        return temp_sum / valid_readings;
    }
    else
    {
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

    for (int i = 0; i < 5; i++)
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
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    gpio_set_level(GPIO_PH_POWER, 0);

    if (valid_readings > 0)
    {
        return ph_sum / valid_readings;
    }
    else
    {
        ESP_LOGE(TAG, "All pH readings failed");
        return NAN;
    }
}

void hardware_manager_set_temp_reading_interval(uint32_t interval_seconds)
{
    if (temp_reading_timer == NULL)
    {
        ESP_LOGE(TAG, "Temperature reading timer not initialized");
        return;
    }

    if (interval_seconds == 0)
    {
        xTimerStop(temp_reading_timer, portMAX_DELAY);
        ESP_LOGI(TAG, "Temperature reading timer stopped");
    }
    else
    {
        TickType_t period_ticks = pdMS_TO_TICKS(interval_seconds * 1000);
        xTimerChangePeriod(temp_reading_timer, period_ticks, portMAX_DELAY);
        xTimerStart(temp_reading_timer, portMAX_DELAY);
        ESP_LOGI(TAG, "Temperature reading timer set to %lu seconds (auto-reload)", interval_seconds);
    }
}

void hardware_manager_set_feeding_interval(uint32_t interval_seconds)
{
    if (feeding_timer == NULL)
    {
        ESP_LOGE(TAG, "Feeding timer not initialized");
        return;
    }

    if (interval_seconds == 0)
    {
        xTimerStop(feeding_timer, portMAX_DELAY);
        ESP_LOGI(TAG, "Feeding timer stopped");
    }
    else
    {
        TickType_t period_ticks = pdMS_TO_TICKS(interval_seconds * 1000);
        xTimerChangePeriod(feeding_timer, period_ticks, portMAX_DELAY);
        xTimerStart(feeding_timer, portMAX_DELAY);
        ESP_LOGI(TAG, "Feeding timer set to %lu seconds (auto-reload)", interval_seconds);
    }
}

void hardware_manager_motor_rotate_portion(bool direction)
{
    motor_rotate_portion(direction);
}

void hardware_manager_start_beam_monitor(TaskHandle_t *task_handle)
{
    if (task_handle == NULL)
    {
        ESP_LOGE(TAG, "Invalid task handle pointer");
        return;
    }

    xTaskCreate(
        break_beam_monitor,
        "beam_monitor",
        2048,
        task_handle, // Pass task handle pointer so beam_monitor can set it to NULL when beam breaks
        5,
        task_handle);

    ESP_LOGI(TAG, "Beam monitor task started, handle: 0x%p", *task_handle);
}

void hardware_manager_stop_beam_monitor(TaskHandle_t task_handle)
{
    if (task_handle != NULL)
    {
        vTaskDelete(task_handle);
        ESP_LOGI(TAG, "Beam monitor task stopped");
    }
}

void hardware_manager_init(void)
{
    s_hardware_event_group = xEventGroupCreate();

    display_init(GPIO_OLED_SCL, GPIO_OLED_SDA);
    left_button_init(GPIO_LEFT_BUTTON);
    right_button_init(GPIO_RIGHT_BUTTON);
    confirm_button_init(GPIO_CONFIRM_BUTTON);
    break_beam_init(GPIO_BREAK_BEAM);
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

    if (temp_reading_timer == NULL || feeding_timer == NULL)
    {
        ESP_LOGE(TAG, "Failed to create timers");
    }
    else
    {
        ESP_LOGI(TAG, "Timers created successfully");
    }

    ESP_LOGI(TAG, "Hardware manager initialized");
}