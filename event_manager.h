#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#define EVENT_BIT_CONFIG_BUTTON_PRESSED BIT8 // Passkey display mode active
#define EVENT_BIT_CONFIG_MODE BIT0           // GATT server/config mode active
#define EVENT_BIT_PASSKEY_DISPLAY BIT1       // Passkey display mode active
#define EVENT_BIT_WIFI_STATUS BIT2           // WiFi is connected (set=connected, clear=disconnected)
#define EVENT_BIT_WIFI_CLEARED BIT3          // Request to clear WiFi credentials
#define EVENT_BIT_WIFI_CONFIG_SAVED BIT4     // WiFi config was saved via BLE
#define EVENT_BIT_FEED_SCHEDULED BIT5        // Button was pressed
#define EVENT_BIT_FEED_SUCCESSFUL BIT6       // Button was pressed
#define EVENT_BIT_FEED_FAILED BIT7           // Button was pressed
#define EVENT_BIT_BATTERY_LOW BIT9           // Battery level is low
#define EVENT_BIT_DISPLAY_STATUS BIT10       // Display on/off
#define EVENT_BIT_DISPLAY_LEFT BIT11         // Display navigation left
#define EVENT_BIT_DISPLAY_RIGHT BIT12        // Display navigation right
#define EVENT_BIT_DISPLAY_CONFIRM BIT13      // Display navigation confirm
#define EVENT_BIT_DISPLAY_WAKE BIT14         // Display wake up
#define EVENT_BIT_MEASURE_TEMP BIT15         // Request temperature measurement
#define EVENT_BIT_MEASURE_PH BIT16           // Request pH measurement



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
