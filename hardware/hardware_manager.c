#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "hardware_manager.h"
#include "wifi_manager.h"
#include "event_manager.h"
#include "button_driver.h"
#include "ssd1306_demo.h"
#include "ssd1306.h"

static const char *TAG = "hardware_manager";
TaskHandle_t led_task_handle = NULL;

static void led_blink_task(void *pvParameters)
{
    uint32_t notification = 0;
    led_task_handle = xTaskGetCurrentTaskHandle();
    event_manager_register_notification(led_task_handle, EVENT_BIT_WIFI_STATUS);

    while (1)
    {
        EventBits_t bits = event_manager_get_bits();
        if (!(bits & EVENT_BIT_WIFI_STATUS))
        {
            gpio_set_level(BLINK_GPIO, 1);
            xTaskNotifyWait(0, UINT32_MAX, &notification, pdMS_TO_TICKS(BLINK_PERIOD_MS));
            if (notification)
            {
                notification = 0;
                continue;
            }

            gpio_set_level(BLINK_GPIO, 0);
            xTaskNotifyWait(0, UINT32_MAX, &notification, pdMS_TO_TICKS(BLINK_PERIOD_MS));
            if (notification)
            {
                notification = 0;
                continue;
            }
        }
        else
        {
            gpio_set_level(BLINK_GPIO, 0);
            xTaskNotifyWait(0, UINT32_MAX, &notification, portMAX_DELAY);
            notification = 0;
            continue;
        }
    }
}

void init_hardware(void)
{
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BLINK_GPIO, 0);

    if (led_task_handle == NULL)
    {
        xTaskCreate(
            led_blink_task,
            "led_blink_task",
            4096,
            NULL,
            5,
            &led_task_handle);
        ESP_LOGI(TAG, "LED blink task started");
    }

    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = OLED_SCL_GPIO,
        .sda_io_num = OLED_SDA_GPIO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true};

    i2c_new_master_bus(&bus_cfg, &bus);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x3C,
        .scl_speed_hz = 50000};

    i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    oled_init(dev);

    ssd1306_demo_init();
    button_driver_init();
}