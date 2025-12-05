#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#define EVENT_BIT_WIFI_STATUS BIT0            // WiFi is connected (set=connected, clear=disconnected)
#define EVENT_BIT_BUTTON_PRESSED BIT1         // Button was pressed
#define EVENT_BIT_BUTTON_RELEASED BIT2        // Button was released
#define EVENT_BIT_BLE_STATUS BIT3             // BLE device connected
#define EVENT_BIT_BATTERY_LOW BIT4            // Battery level is low
#define EVENT_BIT_SENSOR_READY BIT5           // Sensor data ready
#define EVENT_BIT_WIFI_CONFIG_SAVED BIT6      // WiFi config was saved via BLE
#define EVENT_BIT_WIFI_CLEAR_CREDENTIALS BIT9 // Request to clear WiFi credentials
#define EVENT_BIT_PASSKEY_DISPLAY BIT7        // Passkey display mode active
#define EVENT_BIT_CONFIG_MODE BIT8            // GATT server/config mode active

EventGroupHandle_t event_manager_get_group(void);

void event_manager_init(void);
EventBits_t event_manager_set_bits(EventBits_t bits);
EventBits_t event_manager_clear_bits(EventBits_t bits);
EventBits_t event_manager_get_bits(void);
EventBits_t event_manager_wait_bits(EventBits_t bits_to_wait_for,
                                    bool clear_on_exit,
                                    bool wait_for_all,
                                    TickType_t timeout_ms);
esp_err_t event_manager_register_notification(TaskHandle_t task_handle, EventBits_t event_bits);
esp_err_t event_manager_unregister_notification(TaskHandle_t task_handle, EventBits_t event_bits);

#endif // EVENT_MANAGER_H
