#include "beam_driver.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "event_manager.h"

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

static void break_beam_task(void *arg)
{
    uint32_t io_level;
    bool success = false;

    while (1)
    {
        EventBits_t bits = event_manager_wait_bits(
            EVENT_BIT_FEED_SCHEDULED,
            true,  // Clear on exit
            false, // Wait for any
            portMAX_DELAY);

        if (bits & EVENT_BIT_FEED_SCHEDULED)
        {
            ESP_LOGI(TAG, "Starting monitoring...");
            success = false;
            TickType_t timeout_ticks = pdMS_TO_TICKS(15000);
            TickType_t start_time = xTaskGetTickCount();

            while ((xTaskGetTickCount() - start_time) < timeout_ticks)
            {
                TickType_t remaining_time = timeout_ticks - (xTaskGetTickCount() - start_time);
                if (xQueueReceive(gpio_evt_queue, &io_level, remaining_time) == pdTRUE)
                {
                    if (io_level == 0)
                    {
                        ESP_LOGI(TAG, "Boom");
                        success = true;
                    }
                }
            }

            if (success)
            {
                event_manager_set_bits(EVENT_BIT_FEED_SUCCESSFUL);
            }
            else
            {
                event_manager_set_bits(EVENT_BIT_FEED_FAILED);
            }

            ESP_LOGI(TAG, "Stopping monitoring...");
        }
    }
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

    xTaskCreate(break_beam_task, "break_beam_task", 2048, NULL, 5, NULL);
    ESP_LOGI(TAG, "Break beam driver initialized (GPIO %d)", gpio);
}