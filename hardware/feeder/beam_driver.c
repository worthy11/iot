#include "beam_driver.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "break_beam";
static QueueHandle_t gpio_evt_queue = NULL;
static gpio_num_t beam_gpio = GPIO_NUM_NC;

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
    // Note: Cannot use ESP_LOG in ISR, but interrupt is being handled
}

void break_beam_monitor(void *pvParameters)
{
    TaskHandle_t *task_handle = (TaskHandle_t *)pvParameters;
    uint32_t io_level;

    if (task_handle == NULL)
    {
        ESP_LOGE(TAG, "Beam monitor task: NULL task handle pointer!");
        vTaskDelete(NULL);
        return;
    }

    if (gpio_evt_queue == NULL)
    {
        ESP_LOGE(TAG, "Beam monitor task: GPIO event queue is NULL!");
        *task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

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

void break_beam_init(gpio_num_t gpio)
{
    beam_gpio = gpio;

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

int break_beam_get_state(void)
{
    if (beam_gpio == GPIO_NUM_NC)
    {
        return -1; // Not initialized
    }
    return gpio_get_level(beam_gpio);
}