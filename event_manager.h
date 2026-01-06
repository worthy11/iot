#ifndef EVENT_MANAGER_H
#define EVENT_MANAGER_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "data/aquarium_data.h"

// BLE events
#define EVENT_BIT_CONFIG_MODE BIT0
#define EVENT_BIT_PASSKEY_DISPLAY BIT1
#define EVENT_BIT_WIFI_SAVED BIT2
#define EVENT_BIT_WIFI_CLEARED BIT3
#define EVENT_BIT_PROVISION_TRIGGER BIT4

// Network status events
#define EVENT_BIT_WIFI_STATUS BIT5
#define EVENT_BIT_MQTT_STATUS BIT6

// Display events
#define EVENT_BIT_DISPLAY_NEXT BIT7
#define EVENT_BIT_DISPLAY_PREV BIT8
#define EVENT_BIT_DISPLAY_CONFIRM BIT9

// Measurement events
#define EVENT_BIT_TEMP_SCHEDULED BIT10
#define EVENT_BIT_TEMP_RESCHEDULED BIT11
#define EVENT_BIT_TEMP_UPDATED BIT12

#define EVENT_BIT_PH_SCHEDULED BIT13
#define EVENT_BIT_PH_UPDATED BIT14
#define EVENT_BIT_PH_CONFIRMED BIT15

// Feeding events
#define EVENT_BIT_FEED_SCHEDULED BIT16
#define EVENT_BIT_FEED_RESCHEDULED BIT17
#define EVENT_BIT_FEED_UPDATED BIT18

#define EVENT_BIT_BATTERY_LOW BIT19

void event_manager_init(void);
EventBits_t event_manager_set_bits(EventBits_t bits);
EventBits_t event_manager_clear_bits(EventBits_t bits);
EventBits_t event_manager_get_bits(void);
EventBits_t event_manager_wait_bits(EventBits_t bits_to_wait_for,
                                    bool clear_on_exit,
                                    bool wait_for_all,
                                    TickType_t timeout_ms);

void event_manager_get_aquarium_data(aquarium_data_t *data);
uint32_t event_manager_get_passkey(void);
void event_manager_register_notification(TaskHandle_t task_handle, EventBits_t bits);

#endif // EVENT_MANAGER_H
