#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <time.h>

// BLE events
#define EVENT_BIT_PROVISIONING_CHANGED BIT0

#define EVENT_BIT_BLE_ADVERTISING BIT1
#define EVENT_BIT_BLE_CONNECTED BIT2     // Device connected
#define EVENT_BIT_BLE_DISCONNECTED BIT3  // Device disconnected
#define EVENT_BIT_PASSKEY_DISPLAY BIT4   // Device disconnected
#define EVENT_BIT_PAIRING_MODE_ON BIT21  // Pairing mode enabled
#define EVENT_BIT_PAIRING_MODE_OFF BIT22 // Pairing mode disabled

// Display events
#define EVENT_BIT_DISPLAY_NEXT BIT6
#define EVENT_BIT_DISPLAY_PREV BIT7
#define EVENT_BIT_DISPLAY_CONFIRM BIT8

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
#define EVENT_BIT_TIME_SYNC BIT20
#define EVENT_BIT_OTA_UPDATE BIT19

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
void event_manager_set_temp_lower(float threshold);
void event_manager_set_temp_upper(float threshold);
void event_manager_set_ph_lower(float threshold);
void event_manager_set_ph_upper(float threshold);

float event_manager_get_temp_lower(void);
float event_manager_get_temp_upper(void);
float event_manager_get_ph_lower(void);
float event_manager_get_ph_upper(void);

uint32_t event_manager_get_feeding_interval(void);
uint32_t event_manager_get_temp_reading_interval(void);
uint32_t event_manager_get_publish_interval(void);

void event_manager_activity_counter_increment(void);
void event_manager_activity_counter_decrement(void);

int64_t event_manager_get_current_timestamp_ms(void);

#endif // EVENT_MANAGER_H
