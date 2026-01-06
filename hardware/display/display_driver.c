#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "nvs.h"
#include "nvs_flash.h"

#include "event_manager.h"
#include "display_driver.h"
#include "ssd1306.h"

static const char *TAG = "display_driver";
static const char *NVS_NAMESPACE = "display";

static SemaphoreHandle_t display_mutex = NULL;
static TimerHandle_t sleep_timer = NULL;

typedef struct
{
    bool temperature_display_enabled;
    bool ph_display_enabled;
    bool last_feeding_display_enabled;
    bool next_feeding_display_enabled;
    uint8_t display_contrast;
    uint32_t display_sleep_time_min;
} display_settings_t;

static display_settings_t g_display_settings = {
    .temperature_display_enabled = true,
    .ph_display_enabled = true,
    .last_feeding_display_enabled = true,
    .next_feeding_display_enabled = true,
    .display_contrast = 32,
    .display_sleep_time_min = 1};

static esp_err_t save_display_settings_to_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, "settings", &g_display_settings, sizeof(display_settings_t));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to save display settings: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Display settings saved to NVS");
    }
    return err;
}

static esp_err_t load_display_settings_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        if (save_display_settings_to_nvs() == ESP_OK)
        {
            ESP_LOGI(TAG, "Default display settings saved to NVS");
        }
        return err;
    }

    size_t required_size = sizeof(display_settings_t);
    err = nvs_get_blob(handle, "settings", &g_display_settings, &required_size);
    nvs_close(handle);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to load display settings: %s", esp_err_to_name(err));
        if (save_display_settings_to_nvs() == ESP_OK)
        {
            ESP_LOGI(TAG, "Default display settings saved to NVS");
        }
        return err;
    }

    ESP_LOGI(TAG, "Display settings loaded from NVS");
    return ESP_OK;
}

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

typedef void (*display_func_t)(void);
typedef display_state_t (*state_transition_t)(void);
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

static void reset_sleep_timer(void);

static void display_main_page(void);
static void display_selection(void);
static void display_actions(void);
static void display_settings(void);
static void display_config(void);
static void display_config_mode(void);
static void display_passkey(void);
static void display_ph_measurement_confirmation(void);
static void display_ph_measurement(void);
static void display_temp_measurement(void);
static void display_temp_result(float temp);
static void display_ph_result(float ph);

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

static void reset_sleep_timer(void)
{
    if (sleep_timer != NULL && g_display_settings.display_sleep_time_min > 0)
    {
        xTimerChangePeriod(sleep_timer, pdMS_TO_TICKS(g_display_settings.display_sleep_time_min * 60000), 0);
        xTimerReset(sleep_timer, 0);
    }
}

static void sleep_timer_callback(TimerHandle_t xTimer)
{
    if (event_manager_get_bits() & EVENT_BIT_CONFIG_MODE)
    {
        reset_sleep_timer();
        return;
    }

    if (display_awake)
    {
        display_awake = false;
        oled_display_off();
    }
}

static const char *get_time_string(time_t time_val)
{
    static char time_str[32];
    if (time_val == 0 || time_val < 1000000000)
    {
        snprintf(time_str, sizeof(time_str), "Never");
    }
    else
    {
        struct tm *timeinfo = localtime(&time_val);
        if (timeinfo == NULL)
        {
            snprintf(time_str, sizeof(time_str), "Never");
        }
        else
        {
            int ret = snprintf(time_str, sizeof(time_str), "%02d/%02d %02d:%02d",
                               timeinfo->tm_mday, timeinfo->tm_mon + 1, // DD/MM
                               timeinfo->tm_hour, timeinfo->tm_min);    // HH:MM
            (void)ret;                                                  // Suppress unused variable warning
        }
    }
    return time_str;
}

static void display_main_page(void)
{
    aquarium_data_t data;
    event_manager_get_aquarium_data(&data);
    uint8_t font_size = 1;
    uint8_t line_height = font_size * 8 + 2;
    uint8_t x_indent = 0;
    uint8_t y_pos = line_height + 4;

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text(" <<< STATUS >>> ", font_size, 0);

    char line[64];

    if (g_display_settings.temperature_display_enabled)
    {
        snprintf(line, sizeof(line), "Temp: %.1f C", data.temperature);
        oled_set_position(y_pos, x_indent);
        oled_draw_text(line, font_size, 0);
        y_pos += line_height;
    }

    if (g_display_settings.ph_display_enabled)
    {
        snprintf(line, sizeof(line), "pH: %.2f", data.ph);
        oled_set_position(y_pos, x_indent);
        oled_draw_text(line, font_size, 0);
        y_pos += line_height;
    }

    if (g_display_settings.last_feeding_display_enabled)
    {
        snprintf(line, sizeof(line), "Fed: %s", get_time_string(data.last_feed_time));
        oled_set_position(y_pos, x_indent);
        oled_draw_text(line, font_size, 0);
        y_pos += line_height;
    }

    if (g_display_settings.next_feeding_display_enabled)
    {
        snprintf(line, sizeof(line), "Due: %s", get_time_string(data.next_feed_time));
        oled_set_position(y_pos, x_indent);
        oled_draw_text(line, font_size, 0);
    }

    oled_update_display();
}

static void display_selection(void)
{
    uint8_t font_size = 1;
    uint8_t line_height = font_size * 8 + 2;
    uint8_t y_start = line_height + 4;
    uint8_t x_indent = 0;
    uint8_t x_text = 8;

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text(" <<<  MENU  >>> ", font_size, 0);

    const char *menu_items[] = {"<< BACK", "Actions", "Display Options", "WiFi Config"};
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

static void display_actions(void)
{
    uint8_t font_size = 1;
    uint8_t line_height = font_size * 8 + 2;
    uint8_t y_start = line_height + 4;
    uint8_t x_indent = 0;
    uint8_t x_text = 8;

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text(" <<<ACTIONS>>>", font_size, 0);

    const char *menu_items[] = {"<< BACK", "Feed Fish", "Measure Temp", "Measure pH"};
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

static void display_settings(void)
{
    uint8_t font_size = 1;
    uint8_t line_height = font_size * 8 + 2;
    uint8_t y_start = line_height + 4;
    uint8_t x_indent = 0;
    uint8_t x_text = 8;

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text(" DISPLAY OPTIONS", font_size, 0);

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
                     g_display_settings.temperature_display_enabled ? "ON" : "OFF");
            break;
        case 2:
            snprintf(menu_line, sizeof(menu_line), "pH %s",
                     g_display_settings.ph_display_enabled ? "ON" : "OFF");
            break;
        case 3:
            snprintf(menu_line, sizeof(menu_line), "Fed %s",
                     g_display_settings.last_feeding_display_enabled ? "ON" : "OFF");
            break;
        case 4:
            snprintf(menu_line, sizeof(menu_line), "Due %s",
                     g_display_settings.next_feeding_display_enabled ? "ON" : "OFF");
            break;
        case 5:
            snprintf(menu_line, sizeof(menu_line), "Contrast");
            break;
        case 6:
        {
            uint32_t sleep_time = g_display_settings.display_sleep_time_min;
            if (sleep_time == 0)
            {
                snprintf(menu_line, sizeof(menu_line), "Sleep NEVER");
            }
            else
            {
                snprintf(menu_line, sizeof(menu_line), "Sleep %lu min", (unsigned long)sleep_time);
            }
        }
        break;
        case 7:
            snprintf(menu_line, sizeof(menu_line), "Factory");
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
    uint8_t font_size = 1;
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
    uint8_t font_size = 1;

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text("CONFIG MODE", font_size, 0);

    oled_update_display();
}

static void display_passkey(void)
{
    uint8_t font_size = 1;
    uint8_t line_height = font_size * 8 + 2;
    char passkey_str[16];

    uint32_t passkey = event_manager_get_passkey();
    snprintf(passkey_str, sizeof(passkey_str), "%06lu", (unsigned long)passkey);

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text("Pairing code:", font_size, 0);
    oled_set_position(line_height, 0);
    oled_draw_text(passkey_str, font_size, 0);

    oled_update_display();
}

static void display_ph_measurement_confirmation(void)
{
    uint8_t font_size = 1;
    uint8_t line_height = font_size * 8 + 2;

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text("Measure pH", font_size, 0);
    oled_set_position(line_height, 0);
    oled_draw_text("Press Confirm", font_size, 0);

    oled_update_display();
}

static void display_ph_measurement(void)
{
    uint8_t font_size = 1;
    uint8_t line_height = font_size * 8 + 2;

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text("Measuring", font_size, 0);
    oled_set_position(line_height, 0);
    oled_draw_text("pH...", font_size, 0);

    oled_update_display();
}

static void display_temp_measurement(void)
{
    uint8_t font_size = 1;
    uint8_t line_height = font_size * 8 + 2;

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text("Measuring", font_size, 0);
    oled_set_position(line_height, 0);
    oled_draw_text("Temperature...", font_size, 0);

    oled_update_display();
}

static void display_temp_result(float temp)
{
    uint8_t font_size = 1;
    uint8_t line_height = font_size * 8 + 2;
    char temp_str[32];

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text("Temperature", font_size, 0);
    snprintf(temp_str, sizeof(temp_str), "%.1f C", temp);
    oled_set_position(line_height, 0);
    oled_draw_text(temp_str, font_size, 0);

    oled_update_display();
}

static void display_ph_result(float ph)
{
    uint8_t font_size = 1;
    uint8_t line_height = font_size * 8 + 2;
    char ph_str[32];

    oled_clear_display();
    oled_set_position(0, 0);
    oled_draw_text("pH", font_size, 0);
    snprintf(ph_str, sizeof(ph_str), "%.2f", ph);
    oled_set_position(line_height, 0);
    oled_draw_text(ph_str, font_size, 0);

    oled_update_display();
}

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
    event_manager_set_bits(EVENT_BIT_FEED_SCHEDULED);
}

static void action_measure_temp(void)
{
    event_manager_set_bits(EVENT_BIT_TEMP_SCHEDULED);
}

static void action_measure_ph(void)
{
    event_manager_set_bits(EVENT_BIT_PH_SCHEDULED);
}

static void action_toggle_temp_display(void)
{
    g_display_settings.temperature_display_enabled = !g_display_settings.temperature_display_enabled;
    save_display_settings_to_nvs();
}

static void action_toggle_ph_display(void)
{
    g_display_settings.ph_display_enabled = !g_display_settings.ph_display_enabled;
    save_display_settings_to_nvs();
}

static void action_toggle_last_feed_display(void)
{
    g_display_settings.last_feeding_display_enabled = !g_display_settings.last_feeding_display_enabled;
    save_display_settings_to_nvs();
}

static void action_toggle_next_feed_display(void)
{
    g_display_settings.next_feeding_display_enabled = !g_display_settings.next_feeding_display_enabled;
    save_display_settings_to_nvs();
}

static void action_change_contrast(void)
{
    g_display_settings.display_contrast += 32;
    if (g_display_settings.display_contrast < 32)
        g_display_settings.display_contrast = 32;
    oled_set_contrast(g_display_settings.display_contrast);
    save_display_settings_to_nvs();
}

static void action_change_sleep_time(void)
{
    switch (g_display_settings.display_sleep_time_min)
    {
    case 1:
        g_display_settings.display_sleep_time_min = 2;
        break;
    case 2:
        g_display_settings.display_sleep_time_min = 5;
        break;
    case 5:
        g_display_settings.display_sleep_time_min = 10;
        break;
    case 10:
        g_display_settings.display_sleep_time_min = 30;
        break;
    case 30:
        g_display_settings.display_sleep_time_min = 0; // Never
        break;
    case 0:
    default:
        g_display_settings.display_sleep_time_min = 1;
        break;
    }
    save_display_settings_to_nvs();

    if (sleep_timer != NULL)
    {
        if (g_display_settings.display_sleep_time_min == 0)
        {
            xTimerStop(sleep_timer, 0);
        }
        else
        {
            xTimerChangePeriod(sleep_timer, pdMS_TO_TICKS(g_display_settings.display_sleep_time_min * 60000), 0);
            xTimerReset(sleep_timer, 0);
        }
    }

    ESP_LOGI(TAG, "Display sleep time set to %lu minutes", (unsigned long)g_display_settings.display_sleep_time_min);
}

static void action_change_wifi(void)
{
    event_manager_set_bits(EVENT_BIT_CONFIG_MODE);
}

static void action_reset_wifi(void)
{
    event_manager_set_bits(EVENT_BIT_WIFI_CLEARED);
}

static void action_factory_settings(void)
{
    ESP_LOGI(TAG, "Factory Settings: Resetting to defaults");
    g_display_settings.temperature_display_enabled = true;
    g_display_settings.ph_display_enabled = true;
    g_display_settings.last_feeding_display_enabled = true;
    g_display_settings.next_feeding_display_enabled = true;
    g_display_settings.display_contrast = 32;      // Default contrast
    g_display_settings.display_sleep_time_min = 1; // Default 1 minute
    save_display_settings_to_nvs();

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

void display_wake(void)
{
    if (!display_awake)
    {
        display_awake = true;
        oled_display_on();
    }
    reset_sleep_timer();
}

void display_next(void)
{
    if (sm.state >= STATE_COUNT || (event_manager_get_bits() & EVENT_BIT_CONFIG_MODE))
    {
        return;
    }

    if (state_table[sm.state].on_right != NULL)
    {
        display_state_t new_state = state_table[sm.state].on_right();
        sm.state = new_state;
        display_update();
    }
}

void display_prev(void)
{
    if (sm.state >= STATE_COUNT || (event_manager_get_bits() & EVENT_BIT_CONFIG_MODE))
    {
        return;
    }

    if (state_table[sm.state].on_left != NULL)
    {
        display_state_t new_state = state_table[sm.state].on_left();
        sm.state = new_state;
        display_update();
    }
}

void display_confirm(void)
{
    if (sm.state >= STATE_COUNT)
    {
        return;
    }

    EventBits_t bits = event_manager_get_bits();
    if (bits & EVENT_BIT_CONFIG_MODE)
    {
        event_manager_clear_bits(EVENT_BIT_CONFIG_MODE);
        display_update();
        return;
    }

    if ((bits & EVENT_BIT_PH_SCHEDULED) && !(bits & EVENT_BIT_PH_CONFIRMED))
    {
        event_manager_set_bits(EVENT_BIT_PH_CONFIRMED);
    }

    else if (state_table[sm.state].on_confirm != NULL)
    {
        display_state_t new_state = state_table[sm.state].on_confirm();
        sm.state = new_state;
        display_update();
    }
}

void display_update(void)
{
    if (!display_awake || (event_manager_get_bits() & EVENT_BIT_CONFIG_MODE))
        return;

    if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
    {
        aquarium_data_t data;
        event_manager_get_aquarium_data(&data);

        if (sm.state < STATE_COUNT && state_table[sm.state].display_func != NULL)
        {
            state_table[sm.state].display_func();
        }

        xSemaphoreGive(display_mutex);
    }
}

void display_interrupt(void)
{
    if (!display_awake)
    {
        display_wake();
    }

    if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
    {
        EventBits_t bits = event_manager_get_bits();
        bool config_mode = (bits & EVENT_BIT_CONFIG_MODE) != 0;
        bool passkey_display = (bits & EVENT_BIT_PASSKEY_DISPLAY) != 0;
        bool measure_temp = (bits & EVENT_BIT_TEMP_SCHEDULED) != 0;
        bool measure_ph = (bits & EVENT_BIT_PH_SCHEDULED) != 0;
        bool ph_confirmed = (bits & EVENT_BIT_PH_CONFIRMED) != 0;

        if (passkey_display)
        {
            display_passkey();
        }
        else if (config_mode)
        {
            display_config_mode();
        }
        else if (measure_ph)
        {
            if (ph_confirmed)
            {
                display_ph_measurement();
            }
            else
            {
                display_ph_measurement_confirmation();
            }
        }
        else if (measure_temp)
        {
            display_temp_measurement();
        }
        else
        {
            if (sm.state < STATE_COUNT && state_table[sm.state].display_func != NULL)
            {
                state_table[sm.state].display_func();
            }
        }

        xSemaphoreGive(display_mutex);
    }
}

void display_interrupt_with_value(float value, bool is_temp)
{
    if (!display_awake)
    {
        display_wake();
    }

    if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
    {
        if (is_temp)
        {
            display_temp_result(value);
        }
        else
        {
            display_ph_result(value);
        }

        xSemaphoreGive(display_mutex);
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

    load_display_settings_from_nvs();
    oled_set_contrast(g_display_settings.display_contrast);

    uint32_t sleep_time_min = g_display_settings.display_sleep_time_min;
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

    ESP_LOGI(TAG, "Display driver initialized");
}