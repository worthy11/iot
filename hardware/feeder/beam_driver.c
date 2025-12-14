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
}

bool break_beam_monitor(uint32_t timeout_ms)
{
    uint32_t io_level;
    bool success = false;

    if (gpio_evt_queue == NULL)
    {
        ESP_LOGE(TAG, "Beam driver not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Starting beam monitoring (timeout: %lu ms)", timeout_ms);

    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    TickType_t start_time = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start_time) < timeout_ticks)
    {
        TickType_t remaining_time = timeout_ticks - (xTaskGetTickCount() - start_time);
        if (xQueueReceive(gpio_evt_queue, &io_level, remaining_time) == pdTRUE)
        {
            if (io_level == 0)
            {
                ESP_LOGI(TAG, "Beam break detected - food has fallen");
                success = true;
                break;
            }
        }
    }

    if (success)
    {
        ESP_LOGI(TAG, "Beam monitoring completed - success");
    }
    else
    {
        ESP_LOGW(TAG, "Beam monitoring completed - timeout (no beam break)");
    }

    return success;
}

void break_beam_init(gpio_num_t gpio)
{
    beam_gpio = gpio;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE};
    gpio_config(&io_conf);

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    gpio_install_isr_service(0);
    gpio_isr_handler_add(gpio, beam_isr, NULL);

    ESP_LOGI(TAG, "Break beam driver initialized (GPIO %d)", gpio);
}