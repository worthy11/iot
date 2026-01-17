#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

// BLE events
#define EVENT_BIT_PROVISIONING_CHANGED BIT0

#define EVENT_BIT_BLE_ADVERTISING BIT1
#define EVENT_BIT_BLE_CONNECTED BIT2    // Device connected
#define EVENT_BIT_BLE_DISCONNECTED BIT3 // Device disconnected
#define EVENT_BIT_PAIRING_MODE BIT4     // Pairing mode active
#define EVENT_BIT_PAIRING_SUCCESS BIT18 // Pairing successful
#define EVENT_BIT_PASSKEY_DISPLAY BIT5

// Display events
#define EVENT_BIT_DISPLAY_NEXT BIT6
#define EVENT_BIT_DISPLAY_PREV BIT7
#define EVENT_BIT_DISPLAY_CONFIRM BIT8
#define EVENT_BIT_DISPLAY_STATUS BIT9

// Measurement events
#define EVENT_BIT_TEMP_SCHEDULED BIT10
#define EVENT_BIT_PH_SCHEDULED BIT11
#define EVENT_BIT_PH_CONFIRMED BIT12
#define EVENT_BIT_FEED_SCHEDULED BIT13

#define EVENT_BIT_DEEP_SLEEP BIT14

// Network status events
#define EVENT_BIT_PUBLISH_SCHEDULED BIT15
#define EVENT_BIT_WIFI_STATUS BIT16
#define EVENT_BIT_MQTT_STATUS BIT17

void event_manager_init(void);
EventBits_t event_manager_set_bits(EventBits_t bits);
EventBits_t event_manager_clear_bits(EventBits_t bits);
EventBits_t event_manager_get_bits(void);
EventBits_t event_manager_wait_bits(EventBits_t bits_to_wait_for,
                                    bool clear_on_exit,
                                    bool wait_for_all,
                                    TickType_t timeout_ms);
void event_manager_register_notification(TaskHandle_t task_handle, EventBits_t bits);

uint32_t event_manager_get_passkey(void);
void event_manager_set_feeding_interval(uint32_t feed_interval_seconds);
void event_manager_set_temp_reading_interval(uint32_t temp_interval_seconds);
void event_manager_set_publish_interval(int publish_frequency);

uint32_t event_manager_get_feeding_interval(void);
uint32_t event_manager_get_temp_reading_interval(void);
uint32_t event_manager_get_publish_interval(void);

void event_manager_activity_counter_increment(void);
void event_manager_activity_counter_decrement(void);

#endif // EVENT_MANAGER_H
