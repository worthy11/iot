#include "event_manager.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "event_manager";
static EventGroupHandle_t s_event_group = NULL;

#define MAX_NOTIFICATION_REGISTRATIONS 8

typedef struct
{
    TaskHandle_t task_handle;
    EventBits_t event_bits;
} notification_registration_t;

static notification_registration_t s_notifications[MAX_NOTIFICATION_REGISTRATIONS];
static int s_notification_count = 0;

EventGroupHandle_t event_manager_get_group(void)
{
    return s_event_group;
}

void event_manager_init(void)
{
    if (s_event_group == NULL)
    {
        s_event_group = xEventGroupCreate();
        if (s_event_group == NULL)
        {
            ESP_LOGE(TAG, "Failed to create event group");
        }
        else
        {
            ESP_LOGI(TAG, "Event manager initialized");
        }
    }
}

EventBits_t event_manager_set_bits(EventBits_t bits)
{
    if (s_event_group == NULL)
    {
        ESP_LOGW(TAG, "Event group not initialized, call event_manager_init() first");
        return 0;
    }

    EventBits_t result = xEventGroupSetBits(s_event_group, bits);

    for (int i = 0; i < s_notification_count; i++)
    {
        if (s_notifications[i].task_handle != NULL &&
            (s_notifications[i].event_bits & bits) != 0)
        {
            xTaskNotify(s_notifications[i].task_handle, 1, eSetValueWithOverwrite);
        }
    }

    return result;
}

EventBits_t event_manager_clear_bits(EventBits_t bits)
{
    if (s_event_group == NULL)
    {
        ESP_LOGW(TAG, "Event group not initialized, call event_manager_init() first");
        return 0;
    }

    EventBits_t result = xEventGroupClearBits(s_event_group, bits);

    for (int i = 0; i < s_notification_count; i++)
    {
        if (s_notifications[i].task_handle != NULL &&
            (s_notifications[i].event_bits & bits) != 0)
        {
            xTaskNotify(s_notifications[i].task_handle, 1, eSetValueWithOverwrite);
        }
    }

    return result;
}

EventBits_t event_manager_get_bits(void)
{
    return xEventGroupGetBits(s_event_group);
}

EventBits_t event_manager_wait_bits(EventBits_t bits_to_wait_for,
                                    bool clear_on_exit,
                                    bool wait_for_all,
                                    TickType_t timeout_ms)
{
    if (s_event_group == NULL)
    {
        ESP_LOGW(TAG, "Event group not initialized, call event_manager_init() first");
        return 0;
    }

    return xEventGroupWaitBits(
        s_event_group,
        bits_to_wait_for,
        clear_on_exit ? bits_to_wait_for : 0, // Clear bits on exit if requested
        wait_for_all ? pdTRUE : pdFALSE,      // Wait for all or any
        timeout_ms);
}

esp_err_t event_manager_register_notification(TaskHandle_t task_handle, EventBits_t event_bits)
{
    if (task_handle == NULL)
    {
        task_handle = xTaskGetCurrentTaskHandle();
    }

    if (task_handle == NULL)
    {
        ESP_LOGE(TAG, "Invalid task handle");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_notification_count >= MAX_NOTIFICATION_REGISTRATIONS)
    {
        ESP_LOGE(TAG, "Maximum notification registrations reached");
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < s_notification_count; i++)
    {
        if (s_notifications[i].task_handle == task_handle)
        {
            s_notifications[i].event_bits |= event_bits;
            ESP_LOGI(TAG, "Updated notification registration for task %p, events: 0x%lx",
                     task_handle, (unsigned long)s_notifications[i].event_bits);
            return ESP_OK;
        }
    }

    s_notifications[s_notification_count].task_handle = task_handle;
    s_notifications[s_notification_count].event_bits = event_bits;
    s_notification_count++;

    ESP_LOGI(TAG, "Registered notification for task %p, events: 0x%lx", task_handle, (unsigned long)event_bits);
    return ESP_OK;
}

esp_err_t event_manager_unregister_notification(TaskHandle_t task_handle, EventBits_t event_bits)
{
    if (task_handle == NULL)
    {
        task_handle = xTaskGetCurrentTaskHandle();
    }

    if (task_handle == NULL)
    {
        ESP_LOGE(TAG, "Invalid task handle");
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < s_notification_count; i++)
    {
        if (s_notifications[i].task_handle == task_handle)
        {
            if (event_bits == 0)
            {
                memmove(&s_notifications[i], &s_notifications[i + 1],
                        (s_notification_count - i - 1) * sizeof(notification_registration_t));
                s_notification_count--;
                ESP_LOGI(TAG, "Unregistered all notifications for task %p", task_handle);
            }
            else
            {
                s_notifications[i].event_bits &= ~event_bits;
                if (s_notifications[i].event_bits == 0)
                {
                    memmove(&s_notifications[i], &s_notifications[i + 1],
                            (s_notification_count - i - 1) * sizeof(notification_registration_t));
                    s_notification_count--;
                    ESP_LOGI(TAG, "Unregistered notifications for task %p", task_handle);
                }
                else
                {
                    ESP_LOGI(TAG, "Updated notification registration for task %p, events: 0x%lx",
                             task_handle, (unsigned long)s_notifications[i].event_bits);
                }
            }
            return ESP_OK;
        }
    }

    ESP_LOGW(TAG, "Task %p not found in notification registrations", task_handle);
    return ESP_ERR_NOT_FOUND;
}
