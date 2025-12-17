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

void break_beam_monitor(void *pvParameters)
{
    TaskHandle_t *task_handle = (TaskHandle_t *)pvParameters;
    uint32_t io_level;
    ESP_LOGI(TAG, "Starting beam monitoring");

    while (1)
    {
        if (xQueueReceive(gpio_evt_queue, &io_level, portMAX_DELAY) == pdTRUE)
        {
            if (io_level == 0)
            {
                ESP_LOGI(TAG, "Beam break detected - food has fallen");
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
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE};
    gpio_config(&io_conf);

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    gpio_install_isr_service(0);
    gpio_isr_handler_add(gpio, beam_isr, NULL);

    ESP_LOGI(TAG, "Break beam driver initialized (GPIO %d)", gpio);
}