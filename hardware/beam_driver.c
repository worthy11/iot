#include "beam_driver.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define BEAM_PIN GPIO_NUM_4
static const char *TAG = "BEAM";

static QueueHandle_t gpio_evt_queue = NULL;

static void IRAM_ATTR beam_isr(void *arg)
{
    uint32_t level = (uint32_t)gpio_get_level(BEAM_PIN);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (gpio_evt_queue) {
        xQueueSendFromISR(gpio_evt_queue, &level, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

static void sensor_task(void *arg)
{
    uint32_t io_level;
    while (1) {
        if (xQueueReceive(gpio_evt_queue, &io_level, portMAX_DELAY) == pdTRUE) {
            if (io_level == 0) {
                ESP_LOGI(TAG, "WIĄZKA PRZERWANA");
            } else {
                ESP_LOGI(TAG, "WIĄZKA OK");
            }
        }
    }
}

void sensor_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BEAM_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    gpio_config(&io_conf);

    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BEAM_PIN, beam_isr, NULL);

    xTaskCreate(sensor_task, "sensor_task", 2048, NULL, 5, NULL);
}