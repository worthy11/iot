#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#define EVENT_BIT_CONFIG_BUTTON_PRESSED BIT0 // Passkey display mode active
#define EVENT_BIT_CONFIG_MODE BIT1           // GATT server/config mode active
#define EVENT_BIT_PASSKEY_DISPLAY BIT2       // Passkey display mode active

#define EVENT_BIT_WIFI_STATUS BIT3       // WiFi is connected (set=connected, clear=disconnected)
#define EVENT_BIT_WIFI_CLEARED BIT4      // Request to clear WiFi credentials
#define EVENT_BIT_WIFI_CONFIG_SAVED BIT5 // WiFi config was saved via BLE

#define EVENT_BIT_DISPLAY_STATUS BIT6  // Display on/off
#define EVENT_BIT_DISPLAY_LEFT BIT7    // Display navigation left
#define EVENT_BIT_DISPLAY_RIGHT BIT8   // Display navigation right
#define EVENT_BIT_DISPLAY_CONFIRM BIT9 // Display navigation confirm
#define EVENT_BIT_DISPLAY_WAKE BIT10   // Display wake up

#define EVENT_BIT_FEED_SCHEDULED BIT11 // Feed scheduled
#define EVENT_BIT_FEED_UPDATED BIT12   // Feed completed (success or failure)

#define EVENT_BIT_MEASURE_TEMP BIT13 // Request temperature measurement
#define EVENT_BIT_TEMP_UPDATED BIT14

#define EVENT_BIT_MEASURE_PH BIT15               // Request pH measurement
#define EVENT_BIT_PH_MEASUREMENT_CONFIRMED BIT20 // pH measurement confirmed by user
#define EVENT_BIT_PH_UPDATED BIT16

#define EVENT_BIT_TEMP_INTERVAL_CHANGED BIT17 // Temperature reading interval changed
#define EVENT_BIT_FEED_INTERVAL_CHANGED BIT18 // Feeding interval changed

#define EVENT_BIT_BATTERY_LOW BIT19 // Battery level is low

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
