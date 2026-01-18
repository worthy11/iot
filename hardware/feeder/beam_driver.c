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

void break_beam_init(gpio_num_t gpio, gpio_num_t power_gpio)
{
    beam_gpio = gpio;
    beam_power_gpio = power_gpio;

    // Configure the sensor input GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE, // Enable pull-down so GPIO reads LOW when sensor isn't driving
        .intr_type = GPIO_INTR_ANYEDGE};

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
        ESP_LOGI(TAG, "Break beam power GPIO %d configured (initially OFF)", (int)power_gpio);
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