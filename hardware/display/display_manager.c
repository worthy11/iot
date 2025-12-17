#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "event_manager.h"
#include "ssd1306.h"
#include "display_manager.h"
#include "aquarium_data.h"
#include "gap.h"

static const char *TAG = "display";

static SemaphoreHandle_t display_mutex = NULL;
static TimerHandle_t sleep_timer = NULL;

// Display state enumeration
typedef enum
{
    STATE_MAIN,
    STATE_SELECTION,
    STATE_ACTIONS,
    STATE_SETTINGS,
    STATE_CONFIG,
    STATE_CONFIG_MODE,
    STATE_PASSKEY,
    STATE_COUNT
} display_state_t;

// Display function type
typedef void (*display_func_t)(void);

// State transition handler type (returns new state, or current state if no transition)
typedef display_state_t (*state_transition_t)(void);

// State machine structure
typedef struct
{
    display_state_t state;
    int menu_index;                // Current menu index within the state
    display_func_t display_func;   // Function to draw this state
    state_transition_t on_left;    // Handler for LEFT button
    state_transition_t on_right;   // Handler for RIGHT button
    state_transition_t on_confirm; // Handler for CONFIRM button
} state_machine_t;

static state_machine_t sm = {
    .state = STATE_MAIN,
    .menu_index = 0};

static bool display_awake = false;

static void wake_display(void);
static void sleep_display(void);
static void reset_sleep_timer(void);

// Display functions for each state
static void display_main_page(void);
static void display_selection(void);
static void display_actions(void);
static void display_settings(void);
static void display_config(void);
static void display_config_mode(void);
static void display_passkey(void);

// State transition handlers
static display_state_t transition_main_left(void);
static display_state_t transition_main_right(void);
static display_state_t transition_main_confirm(void);
static display_state_t transition_selection_left(void);
static display_state_t transition_selection_right(void);
static display_state_t transition_selection_confirm(void);
static display_state_t transition_actions_left(void);
static display_state_t transition_actions_right(void);
static display_state_t transition_actions_confirm(void);
static display_state_t transition_settings_left(void);
static display_state_t transition_settings_right(void);
static display_state_t transition_settings_confirm(void);
static display_state_t transition_config_left(void);
static display_state_t transition_config_right(void);
static display_state_t transition_config_confirm(void);

// Action functions
static void action_feed_fish(void);
static void action_measure_temp(void);
static void action_measure_ph(void);
static void action_toggle_temp_display(void);
static void action_toggle_ph_display(void);
static void action_toggle_last_feed_display(void);
static void action_toggle_next_feed_display(void);
static void action_change_contrast(void);
static void action_change_sleep_time(void);
static void action_change_wifi(void);
static void action_reset_wifi(void);
static void action_factory_settings(void);

static void sleep_timer_callback(TimerHandle_t xTimer)
{
    sleep_display();
}

static void wake_display(void)
{
    if (!display_awake)
    {
        display_awake = true;
        oled_display_on();
        reset_sleep_timer();
        ESP_LOGI(TAG, "Display woke up");
    }
    else
    {
        reset_sleep_timer();
    }
}

static void sleep_display(void)
{
    if (display_awake)
    {
        display_awake = false;
        oled_display_off();
        ESP_LOGI(TAG, "Display went to sleep");
    }
}

static void reset_sleep_timer(void)
{
    if (sleep_timer != NULL)
    {
        uint32_t sleep_time_min = aquarium_data_get_display_sleep_time();
        if (sleep_time_min > 0)
        {
            // Update timer period if it changed
            xTimerChangePeriod(sleep_timer, pdMS_TO_TICKS(sleep_time_min * 60000), 0);
            xTimerReset(sleep_timer, 0);
        }
        // If sleep_time_min == 0 (never), don't start/reset the timer
    }
}

static const char *get_time_string(time_t time_val)
{
    static char time_str[16];
    if (time_val == 0)
    {
        snprintf(time_str, sizeof(time_str), "Never");
    }
    else
    {
        struct tm *timeinfo = localtime(&time_val);
        snprintf(time_str, sizeof(time_str), "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
    }
    return time_str;
}

// Display functions for each state
static void display_main_page(void)
{
    aquarium_data_t data;
    aquarium_data_get(&data);
    uint8_t font_size = data.font_size;
    uint8_t line_height = font_size * 8 + 2;
    uint8_t x_indent = 0;
    uint8_t y_pos = line_height;

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text("Aquarium Status", font_size, 0);

    char line[64];

    // Only display status items if they are enabled
    if (data.temperature_display_enabled)
    {
        snprintf(line, sizeof(line), "Temp: %.1f C", data.temperature);
        oled_set_position(y_pos, x_indent);
        oled_draw_text(line, font_size, 0);
        y_pos += line_height;
    }

    if (data.ph_display_enabled)
    {
        snprintf(line, sizeof(line), "pH: %.2f", data.ph);
        oled_set_position(y_pos, x_indent);
        oled_draw_text(line, font_size, 0);
        y_pos += line_height;
    }

    if (data.last_feeding_display_enabled)
    {
        snprintf(line, sizeof(line), "Last: %s", get_time_string(data.last_feed_time));
        oled_set_position(y_pos, x_indent);
        oled_draw_text(line, font_size, 0);
        y_pos += line_height;
    }

    if (data.next_feeding_display_enabled)
    {
        snprintf(line, sizeof(line), "Next: %s", get_time_string(data.next_feed_time));
        oled_set_position(y_pos, x_indent);
        oled_draw_text(line, font_size, 0);
    }

    oled_update_display();
}

static void display_selection(void)
{
    aquarium_data_t data;
    aquarium_data_get(&data);
    uint8_t font_size = data.font_size;
    uint8_t line_height = font_size * 8 + 2;
    uint8_t y_start = line_height;
    uint8_t x_indent = 0;
    uint8_t x_text = 8;

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text("MENU", font_size, 0);

    const char *menu_items[] = {"<< BACK", "Actions", "Display Options", "WiFi Config"};
    const int menu_count = sizeof(menu_items) / sizeof(menu_items[0]);

    for (int i = 0; i < menu_count; i++)
    {
        uint8_t y_pos = y_start + i * line_height;
        if (i == sm.menu_index)
        {
            // Don't show ">" for BACK when selected
            if (i != 0)
            {
                oled_set_position(y_pos, x_indent);
                oled_draw_text(">", font_size, 0);
            }
            oled_set_position(y_pos, x_text);
            oled_draw_text_inverse(menu_items[i], font_size, 0);
        }
        else
        {
            oled_set_position(y_pos, x_text);
            oled_draw_text(menu_items[i], font_size, 0);
        }
    }

    oled_update_display();
}

static void display_actions(void)
{
    aquarium_data_t data;
    aquarium_data_get(&data);
    uint8_t font_size = data.font_size;
    uint8_t line_height = font_size * 8 + 2;
    uint8_t y_start = line_height;
    uint8_t x_indent = 0;
    uint8_t x_text = 8;

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text("Actions", font_size, 0);

    const char *menu_items[] = {"<< BACK", "Feed Fish", "Measure Temp", "Measure pH"};
    const int menu_count = sizeof(menu_items) / sizeof(menu_items[0]);

    for (int i = 0; i < menu_count; i++)
    {
        uint8_t y_pos = y_start + i * line_height;
        if (i == sm.menu_index)
        {
            // Don't show ">" for BACK when selected
            if (i != 0)
            {
                oled_set_position(y_pos, x_indent);
                oled_draw_text(">", font_size, 0);
            }
            oled_set_position(y_pos, x_text);
            oled_draw_text_inverse(menu_items[i], font_size, 0);
        }
        else
        {
            oled_set_position(y_pos, x_text);
            oled_draw_text(menu_items[i], font_size, 0);
        }
    }

    oled_update_display();
}

static void display_settings(void)
{
    aquarium_data_t data;
    aquarium_data_get(&data);
    uint8_t font_size = data.font_size;
    uint8_t line_height = font_size * 8 + 2;
    uint8_t y_start = line_height;
    uint8_t x_indent = 0;
    uint8_t x_text = 8;

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text("Display Options", font_size, 0);

    char menu_line[64];
    const int menu_count = 8;
    int visible_start = 0;
    if (sm.menu_index > 2)
        visible_start = sm.menu_index - 2;
    if (visible_start > menu_count - 5)
        visible_start = menu_count - 5;
    if (visible_start < 0)
        visible_start = 0;

    for (int i = 0; i < 5 && (visible_start + i) < menu_count; i++)
    {
        int idx = visible_start + i;
        uint8_t y_pos = y_start + i * line_height;

        switch (idx)
        {
        case 0:
            snprintf(menu_line, sizeof(menu_line), "<< BACK");
            break;
        case 1:
            snprintf(menu_line, sizeof(menu_line), "Temperature %s",
                     data.temperature_display_enabled ? "ON" : "OFF");
            break;
        case 2:
            snprintf(menu_line, sizeof(menu_line), "pH %s",
                     data.ph_display_enabled ? "ON" : "OFF");
            break;
        case 3:
            snprintf(menu_line, sizeof(menu_line), "Last Feed %s",
                     data.last_feeding_display_enabled ? "ON" : "OFF");
            break;
        case 4:
            snprintf(menu_line, sizeof(menu_line), "Next Feed %s",
                     data.next_feeding_display_enabled ? "ON" : "OFF");
            break;
        case 5:
            snprintf(menu_line, sizeof(menu_line), "Contrast");
            break;
        case 6:
        {
            uint32_t sleep_time = aquarium_data_get_display_sleep_time();
            if (sleep_time == 0)
            {
                snprintf(menu_line, sizeof(menu_line), "Sleep: N");
            }
            else
            {
                snprintf(menu_line, sizeof(menu_line), "Sleep: %lu min", (unsigned long)sleep_time);
            }
        }
        break;
        case 7:
            snprintf(menu_line, sizeof(menu_line), "Factory Settings");
            break;
        }

        if (idx == sm.menu_index)
        {
            if (idx != 0)
            {
                oled_set_position(y_pos, x_indent);
                oled_draw_text(">", font_size, 0);
            }
            oled_set_position(y_pos, x_text);
            oled_draw_text_inverse(menu_line, font_size, 0);
        }
        else
        {
            oled_set_position(y_pos, x_text);
            oled_draw_text(menu_line, font_size, 0);
        }
    }

    oled_update_display();
}

static void display_config(void)
{
    aquarium_data_t data;
    aquarium_data_get(&data);
    uint8_t font_size = data.font_size;
    uint8_t line_height = font_size * 8 + 2;
    uint8_t y_start = line_height;
    uint8_t x_indent = 0;
    uint8_t x_text = 8;

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text("WiFi Config", font_size, 0);

    const char *menu_items[] = {"<< BACK", "Change WiFi", "Reset WiFi"};
    const int menu_count = sizeof(menu_items) / sizeof(menu_items[0]);

    for (int i = 0; i < menu_count; i++)
    {
        uint8_t y_pos = y_start + i * line_height;
        if (i == sm.menu_index)
        {
            if (i != 0)
            {
                oled_set_position(y_pos, x_indent);
                oled_draw_text(">", font_size, 0);
            }
            oled_set_position(y_pos, x_text);
            oled_draw_text_inverse(menu_items[i], font_size, 0);
        }
        else
        {
            oled_set_position(y_pos, x_text);
            oled_draw_text(menu_items[i], font_size, 0);
        }
    }

    oled_update_display();
}

static void display_config_mode(void)
{
    aquarium_data_t data;
    aquarium_data_get(&data);
    uint8_t font_size = data.font_size;

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text("CONFIG MODE", font_size, 0);

    oled_update_display();
}

static void display_passkey(void)
{
    aquarium_data_t data;
    aquarium_data_get(&data);
    uint8_t font_size = data.font_size;
    uint8_t line_height = font_size * 8 + 2;
    char passkey_str[16];

    uint32_t passkey = gap_get_current_passkey();
    snprintf(passkey_str, sizeof(passkey_str), "%06lu", (unsigned long)passkey);

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text("Pairing code:", font_size, 0);
    oled_set_position(line_height, 0);
    oled_draw_text(passkey_str, font_size, 0);

    oled_update_display();
}

static void display_ph_measurement_waiting(void)
{
    aquarium_data_t data;
    aquarium_data_get(&data);
    uint8_t font_size = data.font_size;
    uint8_t line_height = font_size * 8 + 2;

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text("Measure pH", font_size, 0);
    oled_set_position(line_height, 0);
    oled_draw_text("Press Confirm", font_size, 0);

    oled_update_display();
}

static void ph_measurement_confirmation_task(void *pvParameters)
{
    ESP_LOGI(TAG, "pH measurement confirmation task started");

    while (1)
    {
        // Wait for pH measurement request
        EventBits_t bits = event_manager_wait_bits(
            EVENT_BIT_MEASURE_PH,
            true,  // Clear on exit
            false, // Wait for any
            portMAX_DELAY);

        if (bits & EVENT_BIT_MEASURE_PH)
        {
            ESP_LOGI(TAG, "pH measurement requested - showing confirmation screen");

            // Wake display
            wake_display();
            vTaskDelay(pdMS_TO_TICKS(100)); // Give display time to wake

            // Display "Measure pH" message
            if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
            {
                display_ph_measurement_waiting();
                xSemaphoreGive(display_mutex);
            }

            // Wait for confirm button press with 1 minute timeout
            EventBits_t confirm_bits = event_manager_wait_bits(
                EVENT_BIT_DISPLAY_CONFIRM,
                true,                  // Clear on exit
                false,                 // Wait for any
                pdMS_TO_TICKS(60000)); // 1 minute timeout

            bool measurement_confirmed = (confirm_bits & EVENT_BIT_DISPLAY_CONFIRM) != 0;

            if (measurement_confirmed)
            {
                ESP_LOGI(TAG, "pH measurement confirmed by user");
                event_manager_set_bits(EVENT_BIT_PH_MEASUREMENT_CONFIRMED);
            }
            else
            {
                ESP_LOGI(TAG, "pH measurement timeout - returning to menu");
            }

            // Clear display and let display manager refresh to show menu
            if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
            {
                oled_clear_display();
                oled_update_display();
                xSemaphoreGive(display_mutex);
            }
            event_manager_set_bits(EVENT_BIT_DISPLAY_WAKE); // Trigger display refresh
        }
    }
}

// State transition handlers
static display_state_t transition_main_left(void)
{
    // From MAIN, go to SELECTION
    sm.menu_index = 0;
    return STATE_SELECTION;
}

static display_state_t transition_main_right(void)
{
    // From MAIN, go to SELECTION
    sm.menu_index = 0;
    return STATE_SELECTION;
}

static display_state_t transition_main_confirm(void)
{
    // No action on main page
    return STATE_MAIN;
}

static display_state_t transition_selection_left(void)
{
    if (sm.menu_index > 0)
    {
        sm.menu_index--;
        return STATE_SELECTION;
    }
    // Already at BACK (index 0), stay here
    return STATE_SELECTION;
}

static display_state_t transition_selection_right(void)
{
    if (sm.menu_index < 3)
    {
        sm.menu_index++;
        return STATE_SELECTION;
    }
    // Wrap to BACK
    sm.menu_index = 0;
    return STATE_SELECTION;
}

static display_state_t transition_selection_confirm(void)
{
    switch (sm.menu_index)
    {
    case 0:
        // BACK - go to main page
        sm.menu_index = 0;
        return STATE_MAIN;
    case 1:
        sm.menu_index = 0;
        return STATE_ACTIONS;
    case 2:
        sm.menu_index = 0;
        return STATE_SETTINGS;
    case 3:
        sm.menu_index = 0;
        return STATE_CONFIG;
    default:
        return STATE_SELECTION;
    }
}

static display_state_t transition_actions_left(void)
{
    if (sm.menu_index > 0)
    {
        sm.menu_index--;
    }
    return STATE_ACTIONS;
}

static display_state_t transition_actions_right(void)
{
    if (sm.menu_index < 3)
    {
        sm.menu_index++;
    }
    return STATE_ACTIONS;
}

static display_state_t transition_actions_confirm(void)
{
    switch (sm.menu_index)
    {
    case 0:
        sm.menu_index = 0;
        return STATE_SELECTION; // BACK
    case 1:
        action_feed_fish();
        return STATE_ACTIONS;
    case 2:
        action_measure_temp();
        return STATE_ACTIONS;
    case 3:
        action_measure_ph();
        return STATE_ACTIONS;
    default:
        return STATE_ACTIONS;
    }
}

static display_state_t transition_settings_left(void)
{
    if (sm.menu_index > 0)
    {
        sm.menu_index--;
    }
    return STATE_SETTINGS;
}

static display_state_t transition_settings_right(void)
{
    if (sm.menu_index < 7)
    {
        sm.menu_index++;
    }
    return STATE_SETTINGS;
}

static display_state_t transition_settings_confirm(void)
{
    switch (sm.menu_index)
    {
    case 0:
        sm.menu_index = 1;      // Return to settings selection
        return STATE_SELECTION; // BACK
    case 1:
        action_toggle_temp_display();
        return STATE_SETTINGS;
    case 2:
        action_toggle_ph_display();
        return STATE_SETTINGS;
    case 3:
        action_toggle_last_feed_display();
        return STATE_SETTINGS;
    case 4:
        action_toggle_next_feed_display();
        return STATE_SETTINGS;
    case 5:
        action_change_contrast();
        return STATE_SETTINGS;
    case 6:
        action_change_sleep_time();
        return STATE_SETTINGS;
    case 7:
        action_factory_settings();
        return STATE_SETTINGS;
    default:
        return STATE_SETTINGS;
    }
}

static display_state_t transition_config_left(void)
{
    if (sm.menu_index > 0)
    {
        sm.menu_index--;
    }
    return STATE_CONFIG;
}

static display_state_t transition_config_right(void)
{
    if (sm.menu_index < 2)
    {
        sm.menu_index++;
    }
    return STATE_CONFIG;
}

static display_state_t transition_config_confirm(void)
{
    switch (sm.menu_index)
    {
    case 0:
        sm.menu_index = 2;      // Return to config selection
        return STATE_SELECTION; // BACK
    case 1:
        action_change_wifi();
        return STATE_CONFIG;
    case 2:
        action_reset_wifi();
        return STATE_CONFIG;
    default:
        return STATE_CONFIG;
    }
}

static void action_feed_fish(void)
{
    ESP_LOGI(TAG, "Action: Feed Fish");
    event_manager_set_bits(EVENT_BIT_FEED_SCHEDULED);
}

static void action_measure_temp(void)
{
    ESP_LOGI(TAG, "Action: Measure Temperature");
    event_manager_set_bits(EVENT_BIT_MEASURE_TEMP);
}

static void action_measure_ph(void)
{
    ESP_LOGI(TAG, "Action: Measure pH");
    event_manager_set_bits(EVENT_BIT_MEASURE_PH);
}

static void action_toggle_temp_display(void)
{
    aquarium_data_t data;
    aquarium_data_get(&data);
    aquarium_data_set_display_enabled(!data.temperature_display_enabled,
                                      data.ph_display_enabled,
                                      data.last_feeding_display_enabled,
                                      data.next_feeding_display_enabled);
}

static void action_toggle_ph_display(void)
{
    aquarium_data_t data;
    aquarium_data_get(&data);
    aquarium_data_set_display_enabled(data.temperature_display_enabled,
                                      !data.ph_display_enabled,
                                      data.last_feeding_display_enabled,
                                      data.next_feeding_display_enabled);
}

static void action_toggle_last_feed_display(void)
{
    aquarium_data_t data;
    aquarium_data_get(&data);
    aquarium_data_set_display_enabled(data.temperature_display_enabled,
                                      data.ph_display_enabled,
                                      !data.last_feeding_display_enabled,
                                      data.next_feeding_display_enabled);
}

static void action_toggle_next_feed_display(void)
{
    aquarium_data_t data;
    aquarium_data_get(&data);
    aquarium_data_set_display_enabled(data.temperature_display_enabled,
                                      data.ph_display_enabled,
                                      data.last_feeding_display_enabled,
                                      !data.next_feeding_display_enabled);
}

static void action_change_contrast(void)
{
    uint8_t contrast = aquarium_data_get_contrast();
    contrast += 32;
    if (contrast == 255)
        contrast = 32;
    aquarium_data_set_contrast(contrast);
    oled_set_contrast(contrast);
}

static void action_change_sleep_time(void)
{
    uint32_t sleep_time = aquarium_data_get_display_sleep_time();
    // Cycle through: 1, 2, 5, 10, 30, 0 (never)
    switch (sleep_time)
    {
    case 1:
        sleep_time = 2;
        break;
    case 2:
        sleep_time = 5;
        break;
    case 5:
        sleep_time = 10;
        break;
    case 10:
        sleep_time = 30;
        break;
    case 30:
        sleep_time = 0; // Never
        break;
    case 0:
    default:
        sleep_time = 1;
        break;
    }
    aquarium_data_set_display_sleep_time(sleep_time);

    // Update the sleep timer
    if (sleep_timer != NULL)
    {
        if (sleep_time == 0)
        {
            // Never sleep - stop the timer
            xTimerStop(sleep_timer, 0);
        }
        else
        {
            // Update timer period and restart
            xTimerChangePeriod(sleep_timer, pdMS_TO_TICKS(sleep_time * 60000), 0);
            xTimerReset(sleep_timer, 0);
        }
    }

    ESP_LOGI(TAG, "Display sleep time set to %lu minutes", (unsigned long)sleep_time);
}

static void action_change_wifi(void)
{
    ESP_LOGI(TAG, "Config: Change Credentials");
    event_manager_set_bits(EVENT_BIT_CONFIG_BUTTON_PRESSED);
}

static void action_reset_wifi(void)
{
    ESP_LOGI(TAG, "Config: Reset Credentials");
    event_manager_set_bits(EVENT_BIT_WIFI_CLEARED);
}

static void action_factory_settings(void)
{
    ESP_LOGI(TAG, "Factory Settings: Resetting to defaults");
    aquarium_data_set_display_enabled(true, true, true, true);
    aquarium_data_set_contrast(32);          // Default contrast
    aquarium_data_set_display_sleep_time(1); // Default 1 minute

    // Update sleep timer
    if (sleep_timer != NULL)
    {
        xTimerChangePeriod(sleep_timer, pdMS_TO_TICKS(60000), 0);
        xTimerReset(sleep_timer, 0);
    }
}

static const struct
{
    display_func_t display_func;
    state_transition_t on_left;
    state_transition_t on_right;
    state_transition_t on_confirm;
} state_table[STATE_COUNT] = {
    [STATE_MAIN] = {
        .display_func = display_main_page,
        .on_left = transition_main_left,
        .on_right = transition_main_right,
        .on_confirm = transition_main_confirm},
    [STATE_SELECTION] = {.display_func = display_selection, .on_left = transition_selection_left, .on_right = transition_selection_right, .on_confirm = transition_selection_confirm},
    [STATE_ACTIONS] = {.display_func = display_actions, .on_left = transition_actions_left, .on_right = transition_actions_right, .on_confirm = transition_actions_confirm},
    [STATE_SETTINGS] = {.display_func = display_settings, .on_left = transition_settings_left, .on_right = transition_settings_right, .on_confirm = transition_settings_confirm},
    [STATE_CONFIG] = {.display_func = display_config, .on_left = transition_config_left, .on_right = transition_config_right, .on_confirm = transition_config_confirm},
    [STATE_CONFIG_MODE] = {.display_func = display_config_mode, .on_left = NULL, .on_right = NULL, .on_confirm = NULL},
    [STATE_PASSKEY] = {.display_func = display_passkey, .on_left = NULL, .on_right = NULL, .on_confirm = NULL},
};

static void update_display(void)
{
    if (!display_awake)
        return;

    if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
    {
        aquarium_data_t data;
        aquarium_data_get(&data);

        oled_set_contrast(data.display_contrast);

        EventBits_t bits = event_manager_get_bits();
        bool config_mode = (bits & EVENT_BIT_CONFIG_MODE) != 0;
        bool passkey_display = (bits & EVENT_BIT_PASSKEY_DISPLAY) != 0;

        if (config_mode && passkey_display)
        {
            display_passkey();
        }
        else if (config_mode)
        {
            display_config_mode();
        }
        else if (sm.state < STATE_COUNT && state_table[sm.state].display_func != NULL)
        {
            state_table[sm.state].display_func();
        }

        xSemaphoreGive(display_mutex);
    }
}

static void display_task(void *pvParameters)
{
    TaskHandle_t task_handle = xTaskGetCurrentTaskHandle();
    event_manager_register_notification(task_handle, EVENT_BIT_DISPLAY_WAKE | EVENT_BIT_CONFIG_MODE | EVENT_BIT_PASSKEY_DISPLAY);

    uint32_t notification_value;
    aquarium_data_t data;

    display_awake = true;
    oled_display_on();
    reset_sleep_timer();

    update_display();

    while (1)
    {
        if (xTaskNotifyWait(0, ULONG_MAX, &notification_value, pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            if (notification_value & EVENT_BIT_DISPLAY_WAKE)
            {
                wake_display();
                update_display();
            }

            if (notification_value & (EVENT_BIT_CONFIG_MODE | EVENT_BIT_PASSKEY_DISPLAY))
            {
                wake_display();
                update_display();
            }
        }

        if (display_awake && sm.state == STATE_MAIN)
        {
            update_display();
        }
    }
}

static void navigation_task(void *pvParameters)
{
    EventBits_t bits;
    display_state_t new_state;

    while (1)
    {
        bits = event_manager_wait_bits(
            EVENT_BIT_DISPLAY_LEFT | EVENT_BIT_DISPLAY_RIGHT | EVENT_BIT_DISPLAY_CONFIRM,
            true,  // Clear on exit
            false, // Wait for any
            portMAX_DELAY);

        wake_display();

        if (sm.state >= STATE_COUNT)
        {
            continue;
        }

        if (bits & EVENT_BIT_DISPLAY_LEFT)
        {
            if (state_table[sm.state].on_left != NULL)
            {
                new_state = state_table[sm.state].on_left();
                sm.state = new_state;
            }
        }
        else if (bits & EVENT_BIT_DISPLAY_RIGHT)
        {
            if (state_table[sm.state].on_right != NULL)
            {
                new_state = state_table[sm.state].on_right();
                sm.state = new_state;
            }
        }
        else if (bits & EVENT_BIT_DISPLAY_CONFIRM)
        {
            if (state_table[sm.state].on_confirm != NULL)
            {
                new_state = state_table[sm.state].on_confirm();
                sm.state = new_state;
            }
        }

        update_display();
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

    sm.state = STATE_MAIN;
    sm.menu_index = 0;

    if (display_mutex == NULL)
    {
        display_mutex = xSemaphoreCreateMutex();
        if (display_mutex == NULL)
        {
            ESP_LOGE(TAG, "Failed to create display mutex");
            return;
        }
    }

    // Get initial sleep time from aquarium_data
    uint32_t sleep_time_min = aquarium_data_get_display_sleep_time();
    uint32_t sleep_time_ms = (sleep_time_min == 0) ? portMAX_DELAY : (sleep_time_min * 60000);

    sleep_timer = xTimerCreate("display_sleep", pdMS_TO_TICKS(sleep_time_ms), pdFALSE, NULL, sleep_timer_callback);
    if (sleep_timer == NULL)
    {
        ESP_LOGE(TAG, "Failed to create sleep timer");
    }
    else
    {
        if (sleep_time_min > 0)
        {
            xTimerStart(sleep_timer, 0);
        }
    }

    xTaskCreate(
        display_task,
        "display_task",
        4096,
        NULL,
        4,
        NULL);

    xTaskCreate(
        navigation_task,
        "navigation_task",
        4096,
        NULL,
        5,
        NULL);

    xTaskCreate(
        ph_measurement_confirmation_task,
        "ph_confirmation_task",
        2048,
        NULL,
        3,
        NULL);

    ESP_LOGI(TAG, "Display manager initialized");
}
