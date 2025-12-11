// #include "bitmap.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdio.h>

#include "event_manager.h"
#include "ssd1306.h"
#include "display_manager.h"

static const char *TAG = "display";

static SemaphoreHandle_t demo_display_mutex = NULL;
static uint32_t current_passkey = 0;

static void update_display(void)
{
    EventBits_t current_bits = event_manager_get_bits();
    bool config_mode = (current_bits & EVENT_BIT_CONFIG_MODE) != 0;
    bool passkey_display = (current_bits & EVENT_BIT_PASSKEY_DISPLAY) != 0;
    char passkey_str[16];

    if (demo_display_mutex != NULL && xSemaphoreTake(demo_display_mutex, portMAX_DELAY) == pdTRUE)
    {
        if (config_mode)
        {
            if (passkey_display)
            {
                oled_scroll_off();
                oled_set_vertical_offset(0);
                oled_clear_display();
                snprintf(passkey_str, sizeof(passkey_str), "%06lu", (unsigned long)current_passkey);
                oled_set_position(12, 0);
                oled_draw_text("Pairing code:", 1, 0);
                oled_set_position(24, 4);
                oled_draw_text(passkey_str, 2, 0);
                oled_update_display();
            }
            else
            {
                oled_scroll_off();
                oled_set_vertical_offset(0);
                oled_clear_display();
                oled_set_position(16, 16);
                oled_draw_text("CONFIG", 2, 0);
                oled_set_position(32, 32);
                oled_draw_text("MODE", 2, 0);
                oled_update_display();
            }
        }
        else
        {
            oled_scroll_off();
            oled_clear_display();
            oled_set_position(0, 0);
            oled_draw_text("Fih", 2, 0);
            oled_update_display();
        }

        xSemaphoreGive(demo_display_mutex);
    }
}

static void config_display_task(void *pvParameters)
{
    TaskHandle_t task_handle = xTaskGetCurrentTaskHandle();
    event_manager_register_notification(task_handle, EVENT_BIT_PASSKEY_DISPLAY | EVENT_BIT_CONFIG_MODE);
    update_display();

    uint32_t notification_value;

    while (1)
    {
        if (xTaskNotifyWait(0, ULONG_MAX, &notification_value, portMAX_DELAY) == pdTRUE)
        {
            update_display();
        }
    }
}

void display_init(gpio_num_t scl_gpio, gpio_num_t sda_gpio)
{
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = scl_gpio,
        .sda_io_num = sda_gpio,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true};

    i2c_new_master_bus(&bus_cfg, &bus);

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x3C,
        .scl_speed_hz = 50000};

    i2c_master_bus_add_device(bus, &dev_cfg, &dev);
    oled_init(dev);

    if (demo_display_mutex == NULL)
    {
        demo_display_mutex = xSemaphoreCreateMutex();
        if (demo_display_mutex == NULL)
        {
            ESP_LOGE(TAG, "Failed to create display mutex");
            return;
        }
    }

    xTaskCreate(
        config_display_task,
        "config_display",
        4096,
        NULL,
        4,
        NULL);
    ESP_LOGI(TAG, "Config display task started");
}
