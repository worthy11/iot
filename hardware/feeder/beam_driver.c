#include "beam_driver.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <time.h>

#include "hardware_manager.h"

static const char *TAG = "break_beam";
static QueueHandle_t gpio_evt_queue = NULL;
static gpio_num_t beam_gpio = GPIO_NUM_NC;
static gpio_num_t beam_power_gpio = GPIO_NUM_NC;

static void IRAM_ATTR beam_isr(void *arg)
{
    uint32_t level = (uint32_t)gpio_get_level(beam_gpio);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (gpio_evt_queue)
    {
        xQueueSendFromISR(gpio_evt_queue, &level, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken == pdTRUE)
        {
            portYIELD_FROM_ISR();
        }
    }
}

static void break_beam_clear_queue(void)
{
    if (gpio_evt_queue == NULL || beam_gpio == GPIO_NUM_NC)
    {
        return;
    }

    // Disable GPIO interrupts while clearing the queue
    gpio_isr_handler_remove(beam_gpio);

    uint32_t io_level;
    // Clear all pending items from the queue
    while (xQueueReceive(gpio_evt_queue, &io_level, 0) == pdTRUE)
    {
        // Discard all items
    }

    // Re-enable GPIO interrupts
    esp_err_t ret = gpio_isr_handler_add(beam_gpio, beam_isr, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to re-add ISR handler for GPIO %d: %s", (int)beam_gpio, esp_err_to_name(ret));
    }
}

void break_beam_monitor(void *pvParameters)
{
    TaskHandle_t *task_handle = (TaskHandle_t *)pvParameters;
    uint32_t io_level;

    // Clear any stale data from the queue before starting monitoring
    break_beam_clear_queue();

    while (1)
    {
        if (xQueueReceive(gpio_evt_queue, &io_level, portMAX_DELAY) == pdTRUE)
        {
            if (io_level == 0)
            {
                break;
            }
        }
    }

    *task_handle = NULL;
    vTaskDelete(NULL);
}

void break_beam_power_on(void)
{
    // Clear queue before powering on to remove any stale data
    break_beam_clear_queue();

    if (beam_power_gpio != GPIO_NUM_NC)
    {
        gpio_set_level(beam_power_gpio, 1);
        ESP_LOGI(TAG, "Break beam sensor powered on");
        // Small delay to allow sensor to stabilize
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void break_beam_power_off(void)
{
    if (beam_power_gpio != GPIO_NUM_NC)
    {
        gpio_set_level(beam_power_gpio, 0);
        ESP_LOGI(TAG, "Break beam sensor powered off");
    }
}

bool break_beam_is_sensor_working(void)
{
    if (beam_gpio == GPIO_NUM_NC)
    {
        ESP_LOGW(TAG, "Beam GPIO not initialized");
        return false;
    }

    // Emitter is powered by GPIO26, receiver is always powered from 3.3V
    // First, ensure emitter is ON
    if (beam_power_gpio != GPIO_NUM_NC)
    {
        gpio_set_level(beam_power_gpio, 1);
        ESP_LOGI(TAG, "Emitter powered on (GPIO %d), checking receiver GPIO %d", (int)beam_power_gpio, (int)beam_gpio);
    }
    else
    {
        ESP_LOGW(TAG, "Emitter power GPIO not configured, checking receiver GPIO %d", (int)beam_gpio);
    }

    // Wait for sensor to stabilize after emitter power-on
    vTaskDelay(pdMS_TO_TICKS(100));

    // Check initial state
    int initial_level = gpio_get_level(beam_gpio);
    ESP_LOGI(TAG, "Initial GPIO %d level: %d", (int)beam_gpio, initial_level);

    // Read the GPIO level multiple times to ensure it's stable and not just noise
    // A working sensor should consistently drive HIGH when beam is unbroken (emitter ON)
    int high_count = 0;
    int low_count = 0;
    const int num_samples = 10;

    for (int i = 0; i < num_samples; i++)
    {
        int level = gpio_get_level(beam_gpio);
        if (level == 1)
        {
            high_count++;
        }
        else
        {
            low_count++;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "Sensor check (emitter ON): HIGH=%d, LOW=%d (out of %d samples)", high_count, low_count, num_samples);
    ESP_LOGI(TAG, "Expected: HIGH when beam is unbroken (emitter ON, receiver detects signal)");

    // Sensor is working if GPIO consistently reads HIGH when emitter is ON (beam unbroken)
    // Require at least 80% HIGH readings to account for any brief noise
    bool is_working = (high_count >= (num_samples * 8 / 10));

    if (!is_working)
    {
        ESP_LOGW(TAG, "Sensor check failed: Only %d%% HIGH readings (need at least 80%%)", (high_count * 100 / num_samples));
    }

    if (is_working && beam_power_gpio != GPIO_NUM_NC)
    {
        // Verify that the sensor responds to emitter: when emitter is OFF, GPIO should read LOW
        // This confirms the receiver is actually detecting the emitter signal
        gpio_set_level(beam_power_gpio, 0);
        ESP_LOGI(TAG, "Emitter turned OFF for verification");
        vTaskDelay(pdMS_TO_TICKS(50));

        int level_off = gpio_get_level(beam_gpio);

        // Turn emitter back on
        gpio_set_level(beam_power_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(10));

        if (level_off != 0)
        {
            ESP_LOGW(TAG, "Sensor verification failed: GPIO did not go LOW when emitter OFF (read %d)", level_off);
            is_working = false;
        }
        else
        {
            ESP_LOGI(TAG, "Sensor verified: GPIO goes LOW when emitter OFF, confirming receiver detects emitter signal");
        }
    }

    if (!is_working)
    {
        ESP_LOGW(TAG, "Sensor not working: GPIO reads LOW or does not respond to emitter (sensor likely not connected)");
    }

    return is_working;
}

void break_beam_init(gpio_num_t gpio, gpio_num_t power_gpio)
{
    beam_gpio = gpio;
    beam_power_gpio = power_gpio;

    // Configure the sensor input GPIO
    // Note: If sensor has internal pull-up/pull-down, you may need to disable external pull-down
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE, // Enable pull-down so GPIO reads LOW when sensor isn't driving
        .intr_type = GPIO_INTR_ANYEDGE};

    ESP_LOGI(TAG, "Configuring receiver GPIO %d as INPUT with pull-down", (int)gpio);

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure GPIO %d: %s", (int)gpio, esp_err_to_name(ret));
        return;
    }

    // Configure the power GPIO
    if (power_gpio != GPIO_NUM_NC)
    {
        gpio_config_t power_conf = {
            .pin_bit_mask = (1ULL << power_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE};

        ret = gpio_config(&power_conf);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to configure power GPIO %d: %s", (int)power_gpio, esp_err_to_name(ret));
            return;
        }

        // Initialize power GPIO to OFF
        gpio_set_level(power_gpio, 0);
        ESP_LOGI(TAG, "Emitter power GPIO %d configured as OUTPUT (initially OFF)", (int)power_gpio);
    }

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    if (gpio_evt_queue == NULL)
    {
        ESP_LOGE(TAG, "Failed to create GPIO event queue!");
        return;
    }

    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
        return;
    }

    ret = gpio_isr_handler_add(gpio, beam_isr, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add ISR handler for GPIO %d: %s", (int)gpio, esp_err_to_name(ret));
        return;
    }
}