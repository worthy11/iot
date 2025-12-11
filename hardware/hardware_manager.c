#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "hardware_manager.h"
#include "wifi_manager.h"
#include "event_manager.h"

static const char *TAG = "hardware_manager";
TaskHandle_t led_task_handle = NULL;

void hardware_init(void)
{
    display_init(GPIO_OLED_SCL, GPIO_OLED_SDA);
    config_button_init(GPIO_CONFIG_BUTTON);
    feed_button_init(GPIO_FEED_BUTTON);
    display_button_init(GPIO_DISPLAY_BUTTON);
    break_beam_init(GPIO_BREAK_BEAM);
    motor_driver_init(GPIO_MOTOR_IN1, GPIO_MOTOR_IN2, GPIO_MOTOR_IN3, GPIO_MOTOR_IN4);
    ph_sensor_init(GPIO_PH_OUTPUT, GPIO_PH_TEMP_COMP);
}