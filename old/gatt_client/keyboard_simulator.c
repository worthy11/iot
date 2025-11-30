#include "sdkconfig.h"

#ifndef CONFIG_LOG_MAXIMUM_LEVEL
#define CONFIG_LOG_MAXIMUM_LEVEL 3
#endif

#include "keyboard_simulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/FreeRTOSConfig.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

static const char *tag = "keyboard_sim";

#define MAX_PRESSED_KEYS 6
#define KEY_REPEAT_DELAY_MS 500
#define KEY_REPEAT_INTERVAL_MS 50
#define BUFFER_SIZE 4096
#define CLIPBOARD_SIZE 512

#define ANSI_CURSOR_UP "\033[A"
#define ANSI_CURSOR_DOWN "\033[B"
#define ANSI_CURSOR_RIGHT "\033[C"
#define ANSI_CURSOR_LEFT "\033[D"
#define ANSI_CLEAR_LINE "\033[K"
#define ANSI_SAVE_CURSOR "\033[s"
#define ANSI_RESTORE_CURSOR "\033[u"
#define ANSI_HIDE_CURSOR "\033[?25l"
#define ANSI_SHOW_CURSOR "\033[?25h"

typedef struct
{
    bool enabled;
    char buffer[BUFFER_SIZE];
    size_t buffer_len;
    size_t cursor_pos;
    size_t selection_start;
    size_t selection_end;
    bool has_selection;
    char clipboard[CLIPBOARD_SIZE];

    uint8_t pressed_keys[MAX_PRESSED_KEYS];
    uint32_t last_key_time[MAX_PRESSED_KEYS];
    uint8_t modifiers;
    bool caps_lock;
} keyboard_simulator_t;

static keyboard_simulator_t kb_sim = {0};
static TaskHandle_t key_repeat_task_handle = NULL;

static char hid_usage_to_char(uint8_t usage, uint8_t modifiers, bool caps_lock)
{
    bool shift = (modifiers & 0x02) || caps_lock;

    if (usage >= 0x04 && usage <= 0x1D)
    {
        char c = 'a' + (usage - 0x04);
        return shift ? toupper(c) : c;
    }

    if (usage >= 0x1E && usage <= 0x27)
    {
        if (usage == 0x27)
            return shift ? ')' : '0';
        char num = '1' + (usage - 0x1E);
        if (shift)
        {
            const char shift_nums[] = "!@#$%^&*(";
            return shift_nums[usage - 0x1E];
        }
        return num;
    }

    switch (usage)
    {
    case 0x2D:
        return shift ? '_' : '-';
    case 0x2E:
        return shift ? '+' : '=';
    case 0x2F:
        return shift ? '{' : '[';
    case 0x30:
        return shift ? '}' : ']';
    case 0x31:
        return shift ? '|' : '\\';
    case 0x33:
        return shift ? ':' : ';';
    case 0x34:
        return shift ? '"' : '\'';
    case 0x35:
        return shift ? '~' : '`';
    case 0x36:
        return shift ? '<' : ',';
    case 0x37:
        return shift ? '>' : '.';
    case 0x38:
        return shift ? '?' : '/';
    case 0x2C:
        return ' ';
    default:
        return 0;
    }
}

static void keyboard_simulator_print_char(char c)
{
    if (kb_sim.cursor_pos < BUFFER_SIZE - 1)
    {
        bool had_selection = kb_sim.has_selection;

        if (kb_sim.has_selection)
        {
            size_t sel_start = kb_sim.selection_start < kb_sim.selection_end ? kb_sim.selection_start : kb_sim.selection_end;
            size_t sel_end = kb_sim.selection_start < kb_sim.selection_end ? kb_sim.selection_end : kb_sim.selection_start;
            size_t sel_len = sel_end - sel_start;

            memmove(kb_sim.buffer + sel_start, kb_sim.buffer + sel_end,
                    kb_sim.buffer_len - sel_end);
            kb_sim.buffer_len -= sel_len;
            kb_sim.cursor_pos = sel_start;
            kb_sim.has_selection = false;
        }

        memmove(kb_sim.buffer + kb_sim.cursor_pos + 1,
                kb_sim.buffer + kb_sim.cursor_pos,
                kb_sim.buffer_len - kb_sim.cursor_pos);
        kb_sim.buffer[kb_sim.cursor_pos] = c;
        kb_sim.cursor_pos++;
        kb_sim.buffer_len++;
        kb_sim.buffer[kb_sim.buffer_len] = '\0';

        if (had_selection)
        {
            printf("\r%s", ANSI_CLEAR_LINE);
            printf("%.*s", (int)kb_sim.buffer_len, kb_sim.buffer);
        }
        else
        {
            printf("%c", c);
        }
        fflush(stdout);
    }
}

static void keyboard_simulator_delete_char(bool forward)
{
    if (kb_sim.has_selection)
    {
        size_t sel_start = kb_sim.selection_start < kb_sim.selection_end ? kb_sim.selection_start : kb_sim.selection_end;
        size_t sel_end = kb_sim.selection_start < kb_sim.selection_end ? kb_sim.selection_end : kb_sim.selection_start;
        size_t sel_len = sel_end - sel_start;

        memmove(kb_sim.buffer + sel_start, kb_sim.buffer + sel_end,
                kb_sim.buffer_len - sel_end);
        kb_sim.buffer_len -= sel_len;
        kb_sim.cursor_pos = sel_start;
        kb_sim.has_selection = false;

        printf("\r%s", ANSI_CLEAR_LINE);
        printf("%.*s", (int)kb_sim.buffer_len, kb_sim.buffer);
        fflush(stdout);
    }
    else if (forward && kb_sim.cursor_pos < kb_sim.buffer_len)
    {
        memmove(kb_sim.buffer + kb_sim.cursor_pos,
                kb_sim.buffer + kb_sim.cursor_pos + 1,
                kb_sim.buffer_len - kb_sim.cursor_pos - 1);
        kb_sim.buffer_len--;
        kb_sim.buffer[kb_sim.buffer_len] = '\0';

        printf("%.*s ", (int)(kb_sim.buffer_len - kb_sim.cursor_pos),
               kb_sim.buffer + kb_sim.cursor_pos);
        printf("\r");
        if (kb_sim.cursor_pos > 0)
        {
            printf("%.*s", (int)kb_sim.cursor_pos, kb_sim.buffer);
        }
        fflush(stdout);
    }
    else if (!forward && kb_sim.cursor_pos > 0)
    {
        kb_sim.cursor_pos--;
        memmove(kb_sim.buffer + kb_sim.cursor_pos,
                kb_sim.buffer + kb_sim.cursor_pos + 1,
                kb_sim.buffer_len - kb_sim.cursor_pos - 1);
        kb_sim.buffer_len--;
        kb_sim.buffer[kb_sim.buffer_len] = '\0';

        printf("\b");
        printf("%.*s ", (int)(kb_sim.buffer_len - kb_sim.cursor_pos),
               kb_sim.buffer + kb_sim.cursor_pos);
        printf("\r");
        if (kb_sim.cursor_pos > 0)
        {
            printf("%.*s", (int)kb_sim.cursor_pos, kb_sim.buffer);
        }
        fflush(stdout);
    }
}

static void keyboard_simulator_move_cursor(int direction)
{
    bool shift = (kb_sim.modifiers & 0x02) != 0;

    if (shift)
    {
        if (!kb_sim.has_selection)
        {
            kb_sim.selection_start = kb_sim.cursor_pos;
            kb_sim.has_selection = true;
        }
        kb_sim.selection_end = kb_sim.cursor_pos;
    }
    else
    {
        kb_sim.has_selection = false;
    }

    switch (direction)
    {
    case 0: // left
        if (kb_sim.cursor_pos > 0)
        {
            kb_sim.cursor_pos--;
            printf(ANSI_CURSOR_LEFT);
            fflush(stdout);
            if (shift)
                kb_sim.selection_end = kb_sim.cursor_pos;
        }
        break;
    case 1: // right
        if (kb_sim.cursor_pos < kb_sim.buffer_len)
        {
            kb_sim.cursor_pos++;
            printf(ANSI_CURSOR_RIGHT);
            fflush(stdout);
            if (shift)
                kb_sim.selection_end = kb_sim.cursor_pos;
        }
        break;
    case 2: // up
        if (kb_sim.cursor_pos > 0)
        {
            printf("\r");
            kb_sim.cursor_pos = 0;
            fflush(stdout);
            if (shift)
                kb_sim.selection_end = kb_sim.cursor_pos;
        }
        break;
    case 3: // down
        if (kb_sim.cursor_pos < kb_sim.buffer_len)
        {
            size_t move = kb_sim.buffer_len - kb_sim.cursor_pos;
            for (size_t i = 0; i < move; i++)
            {
                printf(ANSI_CURSOR_RIGHT);
            }
            kb_sim.cursor_pos = kb_sim.buffer_len;
            fflush(stdout);
            if (shift)
                kb_sim.selection_end = kb_sim.cursor_pos;
        }
        break;
    }
}

static bool keyboard_simulator_process_key(uint8_t usage, uint8_t modifiers, uint32_t current_time)
{
    switch (usage)
    {
    case 0x28: // enter
        printf("\n");
        fflush(stdout);
        kb_sim.buffer_len = 0;
        kb_sim.cursor_pos = 0;
        kb_sim.buffer[0] = '\0';
        kb_sim.has_selection = false;
        return true;

    case 0x29: // esc
        kb_sim.has_selection = false;
        return false;

    case 0x2A: // backspace
        keyboard_simulator_delete_char(false);
        return true;

    case 0x2B: // tab
        keyboard_simulator_print_char('\t');
        return true;
    case 0x4C: // del
        keyboard_simulator_delete_char(true);
        return true;
    case 0x4F: // right
        keyboard_simulator_move_cursor(1);
        return true;
    case 0x50: // left
        keyboard_simulator_move_cursor(0);
        return true;
    case 0x51: // down
        keyboard_simulator_move_cursor(3);
        return true;
    case 0x52: // up
        keyboard_simulator_move_cursor(2);
        return true;
    case 0x4A: // home
        keyboard_simulator_move_cursor(2);
        return false;
    case 0x4D: // end
        keyboard_simulator_move_cursor(3);
        return false;
    case 0x39: // caps lock
        kb_sim.caps_lock = !kb_sim.caps_lock;
        return false;
    }

    char c = hid_usage_to_char(usage, modifiers, kb_sim.caps_lock);
    if (c != 0)
    {
        keyboard_simulator_print_char(c);
        return true;
    }

    return false;
}

void keyboard_simulator_init(void)
{
    memset(&kb_sim, 0, sizeof(kb_sim));
    kb_sim.buffer[0] = '\0';
    ESP_LOGI(tag, "Keyboard simulator initialized");
}

static void key_repeat_task(void *pvParameters)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t repeat_interval = pdMS_TO_TICKS(KEY_REPEAT_INTERVAL_MS);

    ESP_LOGI(tag, "Key repeat task started");

    while (kb_sim.enabled)
    {
        vTaskDelayUntil(&last_wake_time, repeat_interval);

        if (!kb_sim.enabled)
        {
            break;
        }

        TickType_t ticks = xTaskGetTickCount();
        uint32_t current_time = (uint32_t)ticks * 10;
        for (int i = 0; i < MAX_PRESSED_KEYS; i++)
        {
            if (kb_sim.pressed_keys[i] != 0)
            {
                uint32_t time_since_press = current_time - kb_sim.last_key_time[i];
                if (time_since_press >= KEY_REPEAT_DELAY_MS)
                {
                    uint32_t time_since_last_repeat = time_since_press - KEY_REPEAT_DELAY_MS;
                    if (time_since_last_repeat >= KEY_REPEAT_INTERVAL_MS)
                    {
                        keyboard_simulator_process_key(kb_sim.pressed_keys[i], kb_sim.modifiers, current_time);
                        kb_sim.last_key_time[i] = current_time - KEY_REPEAT_DELAY_MS;
                    }
                }
            }
        }
    }

    ESP_LOGI(tag, "Key repeat task ending");
    key_repeat_task_handle = NULL;
    vTaskDelete(NULL);
}

void keyboard_simulator_set_enabled(bool enabled)
{
    kb_sim.enabled = enabled;
    if (enabled)
    {
        printf("\n" ANSI_SHOW_CURSOR);
        ESP_LOGI(tag, "Keyboard simulator enabled");

        if (key_repeat_task_handle == NULL)
        {
            BaseType_t rc = xTaskCreate(key_repeat_task, "key_repeat", 2048, NULL, 5, &key_repeat_task_handle);
            if (rc != pdPASS)
            {
                ESP_LOGE(tag, "Failed to create key repeat task");
                key_repeat_task_handle = NULL;
            }
        }
    }
    else
    {
        printf(ANSI_HIDE_CURSOR "\n");
        ESP_LOGI(tag, "Keyboard simulator disabled");

        if (key_repeat_task_handle != NULL)
        {
            TaskHandle_t task_to_delete = key_repeat_task_handle;
            key_repeat_task_handle = NULL;
            vTaskDelete(task_to_delete);
        }
    }
    fflush(stdout);
}

void keyboard_simulator_process_report(const uint8_t *data, uint16_t len)
{
    if (!kb_sim.enabled || len < 2)
    {
        return;
    }

    uint8_t modifiers = data[0];
    TickType_t ticks = xTaskGetTickCount();
    uint32_t current_time = (uint32_t)ticks * 10;

    uint8_t new_keys[MAX_PRESSED_KEYS] = {0};
    uint8_t new_count = 0;

    int key_start_idx = 1;
    for (int i = key_start_idx; i < len && new_count < MAX_PRESSED_KEYS; i++)
    {
        if (data[i] != 0)
        {
            if (data[i] < 0xE0 || data[i] > 0xE7)
            {
                new_keys[new_count++] = data[i];
            }
        }
    }

    for (int i = 0; i < MAX_PRESSED_KEYS; i++)
    {
        if (kb_sim.pressed_keys[i] != 0)
        {
            bool still_pressed = false;
            for (int j = 0; j < new_count; j++)
            {
                if (kb_sim.pressed_keys[i] == new_keys[j])
                {
                    still_pressed = true;
                    break;
                }
            }

            if (!still_pressed)
            {
                kb_sim.pressed_keys[i] = 0;
            }
        }
    }

    for (int i = 0; i < new_count; i++)
    {
        bool already_pressed = false;
        int existing_idx = -1;

        for (int j = 0; j < MAX_PRESSED_KEYS; j++)
        {
            if (kb_sim.pressed_keys[j] == new_keys[i])
            {
                already_pressed = true;
                existing_idx = j;
                break;
            }
        }

        if (!already_pressed)
        {
            int free_slot = -1;
            for (int j = 0; j < MAX_PRESSED_KEYS; j++)
            {
                if (kb_sim.pressed_keys[j] == 0)
                {
                    free_slot = j;
                    break;
                }
            }

            if (free_slot >= 0)
            {
                bool should_repeat = keyboard_simulator_process_key(new_keys[i], modifiers, current_time);
                if (should_repeat)
                {
                    kb_sim.pressed_keys[free_slot] = new_keys[i];
                    kb_sim.last_key_time[free_slot] = current_time;
                }
            }
        }
    }

    kb_sim.modifiers = modifiers;
}
