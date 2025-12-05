#include "ssd1306.h"
#include "bitmap.h"
#include "event_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ssd1306_demo";

static SemaphoreHandle_t demo_display_mutex = NULL;
static uint32_t current_passkey = 0;

void ssd1306_demo_run(void)
{
    const uint32_t demo_duration_ms = 2000; // 2 seconds per demo

    if (demo_display_mutex == NULL)
    {
        demo_display_mutex = xSemaphoreCreateMutex();
        if (demo_display_mutex == NULL)
        {
            ESP_LOGE(TAG, "Failed to create display mutex");
            return;
        }
    }

    for (uint8_t demo_step = 0; demo_step <= 9; demo_step++)
    {
        if (demo_display_mutex != NULL && xSemaphoreTake(demo_display_mutex, portMAX_DELAY) == pdTRUE)
        {
            switch (demo_step)
            {
            case 0: // Text sizes
                oled_scroll_off();
                oled_clear_display();
                oled_set_position(0, 0);
                oled_draw_text("AquaTest", 1, 0);
                oled_set_position(16, 0);
                oled_draw_text("AquaTest", 2, 0);
                oled_update_display();
                break;

            case 1: // Text rotations
                oled_clear_display();
                oled_set_position(0, 24);
                oled_draw_text("AquaTest", 1, 0);

                oled_set_position(0, 120);
                oled_draw_text("AquaTest", 1, 90);

                oled_set_position(56, 104);
                oled_draw_text("AquaTest", 1, 180);

                oled_set_position(56, 0);
                oled_draw_text("AquaTest", 1, 270);
                oled_update_display();
                vTaskDelay(pdMS_TO_TICKS(2000));
                break;

            case 2: // Inverse text
                oled_clear_display();
                oled_set_position(0, 0);
                oled_draw_text("AquaTest", 1, 0);
                oled_set_position(12, 0);
                oled_draw_text_inverse("AquaTest", 1, 0);

                oled_set_position(24, 0);
                oled_draw_text("AquaTest", 2, 0);
                oled_set_position(48, 0);
                oled_draw_text_inverse("AquaTest", 2, 0);
                oled_update_display();
                vTaskDelay(pdMS_TO_TICKS(2000));
                break;

            case 3: // Contrast
                oled_clear_display();
                oled_set_position(24, 0);
                oled_draw_text("AquaTest", 2, 0);
                oled_update_display();
                for (int i = 1; i < 7; i++)
                {
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    oled_set_contrast((i + 1) * 32);
                }
                oled_set_contrast(32);
                break;

            case 4: // Invert display
                for (int i = 0; i < 2; i++)
                {
                    oled_invert_display();
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    oled_normal_display();
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                break;

            case 5: // Horizontal scroll
                oled_scroll_horizontal(SCROLL_HORIZONTAL_LEFT, 50, 0, 7);
                vTaskDelay(pdMS_TO_TICKS(2000));
                oled_scroll_horizontal(SCROLL_HORIZONTAL_RIGHT, 50, 0, 7);
                vTaskDelay(pdMS_TO_TICKS(2000));
                break;

            case 6: // Vertical scroll
                oled_scroll_off();
                oled_scroll_vertical(SCROLL_VERTICAL_UP, 20);
                vTaskDelay(pdMS_TO_TICKS(2000));
                oled_scroll_vertical(SCROLL_VERTICAL_DOWN, 20);
                vTaskDelay(pdMS_TO_TICKS(2000));
                break;

            case 7: // Diagonal scroll
                oled_scroll_off();
                oled_scroll_diagonal(SCROLL_VERTICAL_UP, SCROLL_HORIZONTAL_RIGHT, 50, 1);
                vTaskDelay(pdMS_TO_TICKS(2000));
                oled_scroll_diagonal(SCROLL_VERTICAL_DOWN, SCROLL_HORIZONTAL_RIGHT, 50, 1);
                vTaskDelay(pdMS_TO_TICKS(2000));
                oled_scroll_diagonal(SCROLL_VERTICAL_UP, SCROLL_HORIZONTAL_LEFT, 50, 1);
                vTaskDelay(pdMS_TO_TICKS(2000));
                oled_scroll_diagonal(SCROLL_VERTICAL_DOWN, SCROLL_HORIZONTAL_LEFT, 50, 1);
                vTaskDelay(pdMS_TO_TICKS(2000));
                break;

            case 8: // Flip
                oled_scroll_off();
                oled_flip_horizontal(false);
                vTaskDelay(pdMS_TO_TICKS(2000));
                oled_flip_vertical(false);
                vTaskDelay(pdMS_TO_TICKS(2000));
                oled_flip_horizontal(true);
                vTaskDelay(pdMS_TO_TICKS(2000));
                oled_flip_vertical(true);
                vTaskDelay(pdMS_TO_TICKS(2000));
                break;

            case 9: // Bitmap
                oled_clear_display();
                oled_set_vertical_offset(0);
                oled_set_position(0, 32);
                oled_draw_text("Fish", 2, 0);
                oled_set_position(20, 48);
                oled_draw_bitmap(fish_image_data, FISH_IMAGE_DATA_WIDTH, FISH_IMAGE_DATA_HEIGHT);
                oled_update_display();
                oled_scroll_horizontal(SCROLL_HORIZONTAL_LEFT, 50, 2, 7);
                break;
            }

            xSemaphoreGive(demo_display_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(demo_duration_ms));
    }
}

static void config_display_task(void *pvParameters)
{
    TaskHandle_t task_handle = xTaskGetCurrentTaskHandle();
    event_manager_register_notification(task_handle, EVENT_BIT_PASSKEY_DISPLAY | EVENT_BIT_CONFIG_MODE);

    uint32_t notification_value;
    char passkey_str[16];

    while (1)
    {
        if (xTaskNotifyWait(0, ULONG_MAX, &notification_value, portMAX_DELAY) == pdTRUE)
        {
            EventBits_t current_bits = event_manager_get_bits();
            bool config_mode = (current_bits & EVENT_BIT_CONFIG_MODE) != 0;
            bool passkey_display = (current_bits & EVENT_BIT_PASSKEY_DISPLAY) != 0;

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
                    // Config mode is not active - clear the screen
                    oled_scroll_off();
                    oled_set_vertical_offset(0);
                    oled_clear_display();
                    oled_update_display();
                }

                xSemaphoreGive(demo_display_mutex);
            }
        }
    }
}

void ssd1306_demo_display_passkey(uint32_t passkey)
{
    current_passkey = passkey;
    event_manager_set_bits(EVENT_BIT_PASSKEY_DISPLAY);
}

void ssd1306_demo_clear_passkey(void)
{
    event_manager_clear_bits(EVENT_BIT_PASSKEY_DISPLAY);
    current_passkey = 0;
}

void ssd1306_demo_init()
{
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
