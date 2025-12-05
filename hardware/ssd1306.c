#include "ssd1306_commands.h"
#include "ssd1306.h"
#include "font.h"
#include "bitmap.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#define FRAMEBUFFER_PAGES (OLED_HEIGHT / 8)

static i2c_master_dev_handle_t oled_dev = NULL;
static uint8_t cursor_col = 0;
static uint8_t cursor_row = 0;

static uint8_t framebuffer[FRAMEBUFFER_PAGES][OLED_WIDTH];
static SemaphoreHandle_t framebuffer_mutex = NULL;
static SemaphoreHandle_t display_mutex = NULL;
static TaskHandle_t scroll_task_handle = NULL;

static volatile oled_scroll_dir_t scroll_type = SCROLL_NONE;
static volatile uint8_t current_first_line = 0;
static volatile uint8_t saved_start_page = 0;
static volatile uint8_t saved_end_page = 7;
static volatile uint32_t saved_speed_ms = 0;

typedef struct
{
    oled_scroll_dir_t direction;
    uint32_t speed_ms;
} scroll_task_params_t;

static scroll_task_params_t *scroll_task_params = NULL;

#define oled_write(payload, is_command) \
    oled_write_impl(payload, sizeof(payload), is_command)

static void oled_write_impl(const uint8_t *payload, size_t len, bool is_command)
{
    uint8_t buffer[len + 1];
    buffer[0] = is_command ? 0x00 : 0x40;
    memcpy(&buffer[1], payload, len);

    i2c_master_transmit(oled_dev, buffer, sizeof(buffer), -1);
}

static void oled_set_column_address(uint8_t start, uint8_t end)
{
    uint8_t cmd[] = {OLED_SET_COLUMN_ADDR, start, end};
    oled_write(cmd, true);
}

static void oled_set_page_address(uint8_t start, uint8_t end)
{
    uint8_t cmd[] = {OLED_SET_PAGE_ADDR, start, end};
    oled_write(cmd, true);
}

void oled_flip_horizontal(bool flip)
{
    if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
    {
        uint8_t cmd[] = {flip ? OLED_SET_SEGMENT_REMAP_1 : OLED_SET_SEGMENT_REMAP_0};
        oled_write(cmd, true);
        xSemaphoreGive(display_mutex);
    }
}

void oled_flip_vertical(bool flip)
{
    if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
    {
        uint8_t cmd[] = {flip ? OLED_SET_COM_SCAN_DEC : OLED_SET_COM_SCAN_INC};
        oled_write(cmd, true);
        xSemaphoreGive(display_mutex);
    }
}

void oled_set_vertical_offset(uint8_t offset)
{
    offset &= 0x3F; // Clamp to valid range (0-63)

    while (current_first_line != offset)
        oled_scroll_line(SCROLL_VERTICAL_DOWN);
}

static void oled_set_memory_addressing_mode(uint8_t mode)
{
    mode &= 0x03; // Clamp to valid range (0-3)
    uint8_t cmd[] = {OLED_SET_MEMORY_MODE, mode};
    oled_write(cmd, true);
}

void oled_display_on(void)
{
    if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
    {
        uint8_t cmd[] = {OLED_DISPLAY_ON};
        oled_write(cmd, true);
        xSemaphoreGive(display_mutex);
    }
}

void oled_display_off(void)
{
    if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
    {
        uint8_t cmd[] = {OLED_DISPLAY_OFF};
        oled_write(cmd, true);
        xSemaphoreGive(display_mutex);
    }
}

void oled_clear_display()
{
    if (framebuffer_mutex != NULL)
    {
        if (xSemaphoreTake(framebuffer_mutex, portMAX_DELAY) == pdTRUE)
        {
            for (uint8_t page = 0; page < FRAMEBUFFER_PAGES; page++)
            {
                memset(framebuffer[page], 0x00, OLED_WIDTH);
            }
            xSemaphoreGive(framebuffer_mutex);
        }
    }
}

void oled_normal_display(void)
{
    if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
    {
        uint8_t normal_cmd[] = {OLED_NORMAL_DISPLAY};
        oled_write(normal_cmd, true);
        xSemaphoreGive(display_mutex);
    }
}

void oled_invert_display(void)
{
    if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
    {
        uint8_t invert_cmd[] = {OLED_INVERT_DISPLAY};
        oled_write(invert_cmd, true);
        xSemaphoreGive(display_mutex);
    }
}

void oled_set_contrast(uint8_t contrast)
{
    if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
    {
        uint8_t cmd[] = {OLED_SET_CONTRAST, contrast};
        oled_write(cmd, true);
        xSemaphoreGive(display_mutex);
    }
}

void oled_set_position(uint8_t x, uint8_t y)
{
    cursor_row = x & 0x3F;
    cursor_col = y & 0x7F;
    uint8_t page = (cursor_row >> 3) & 0x07;

    oled_set_page_address(page, page);
    oled_set_column_address(cursor_col, cursor_col);
}

static void oled_draw_char(char c, uint8_t font_size, uint16_t rotation, const uint8_t *font_data)
{
    if (font_size < 1)
        font_size = 1;
    else if (font_size > 2)
        font_size = 2;

    rotation = rotation % 360;
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270)
        rotation = 0;

    uint8_t base_size = 8 * font_size;
    uint8_t char_width = base_size;
    uint8_t char_height = base_size;
    uint8_t start_page = (cursor_row >> 3) & 0x07;
    uint8_t num_pages = ((cursor_row + char_height - 1) >> 3) - start_page + 1;
    if (num_pages > 8)
        num_pages = 8;

    for (uint8_t page = 0; page < num_pages; page++)
    {
        uint8_t page_num = (start_page + page) & 0x07;

        uint8_t char_data[16];
        for (uint8_t col = 0; col < char_width; col++)
        {
            uint8_t column_byte = 0;

            for (uint8_t bit = 0; bit < 8; bit++)
            {
                uint8_t absolute_row = (page_num * 8) + bit;
                uint8_t relative_row = absolute_row - cursor_row;

                if (relative_row < char_height)
                {
                    uint8_t src_row, src_col;
                    uint8_t x, y;

                    x = col;
                    y = relative_row;

                    uint8_t rotated_x, rotated_y;
                    if (rotation == 0)
                    {
                        rotated_x = x;
                        rotated_y = y;
                    }
                    else if (rotation == 90)
                    {
                        rotated_x = y;
                        rotated_y = char_width - 1 - x;
                    }
                    else if (rotation == 180)
                    {
                        rotated_x = char_width - 1 - x;
                        rotated_y = char_height - 1 - y;
                    }
                    else
                    {
                        rotated_x = char_height - 1 - y;
                        rotated_y = x;
                    }

                    src_row = rotated_y / font_size;
                    src_col = rotated_x / font_size;

                    if (src_row < 8 && src_col < 8)
                    {
                        if (font_data[src_row] & (1 << src_col))
                        {
                            column_byte |= (1 << bit);
                        }
                    }
                }
            }

            char_data[col] = column_byte;
        }

        if (framebuffer_mutex != NULL)
        {
            if (xSemaphoreTake(framebuffer_mutex, portMAX_DELAY) == pdTRUE)
            {
                for (uint8_t col = 0; col < char_width && (cursor_col + col) < OLED_WIDTH; col++)
                {
                    framebuffer[page_num][cursor_col + col] = char_data[col];
                }
                xSemaphoreGive(framebuffer_mutex);
            }
        }
    }

    if (rotation == 0)
    {
        if (cursor_col + char_width < OLED_WIDTH)
            cursor_col += char_width;
        else
        {
            cursor_col = 0;
            cursor_row = cursor_row < OLED_HEIGHT - char_height ? cursor_row + char_height : 0;
        }
    }
    else if (rotation == 90)
    {
        if (cursor_row + char_width < OLED_HEIGHT)
            cursor_row += char_width;
        else
        {
            cursor_row = 0;
            cursor_col = cursor_col >= char_height ? cursor_col - char_height : OLED_WIDTH - char_height;
        }
    }
    else if (rotation == 180)
    {
        if (cursor_col >= char_width)
            cursor_col -= char_width;
        else
        {
            cursor_col = OLED_WIDTH - char_width;
            cursor_row = cursor_row >= char_height ? cursor_row - char_height : OLED_HEIGHT - char_height;
        }
    }
    else if (rotation == 270)
    {
        if (cursor_row >= char_width)
            cursor_row -= char_width;
        else
        {
            cursor_row = OLED_HEIGHT - char_width;
            cursor_col = cursor_col < OLED_WIDTH - char_height ? cursor_col + char_height : 0;
        }
    }
}

void oled_draw_text(const char *str, uint8_t font_size, uint16_t rotation)
{
    uint8_t temp_row = cursor_row;
    uint8_t temp_col = cursor_col;

    while (*str != '\0')
    {
        uint8_t char_code = (uint8_t)(*str) & 0x7F;
        const uint8_t *font_data = (const uint8_t *)font8x8_basic[char_code];
        oled_draw_char(*str, font_size, rotation, font_data);
        str++;
    }
    cursor_row = temp_row;
    cursor_col = temp_col;
}

static void oled_draw_char_inverse(char c, uint8_t font_size, uint16_t rotation)
{
    uint8_t char_code = (uint8_t)c & 0x7F;
    const uint8_t *original_font = (const uint8_t *)font8x8_basic[char_code];

    uint8_t inverted_font[8];
    for (int i = 0; i < 8; i++)
    {
        inverted_font[i] = ~original_font[i];
    }

    oled_draw_char(c, font_size, rotation, inverted_font);
}

void oled_draw_text_inverse(const char *str, uint8_t font_size, uint16_t rotation)
{
    while (*str != '\0')
    {
        oled_draw_char_inverse(*str, font_size, rotation);
        str++;
    }
}

void oled_draw_bitmap(const uint8_t *data, uint8_t image_width, uint8_t image_height)
{
    uint8_t draw_width = image_width;
    uint8_t draw_height = image_height;
    if (cursor_col + draw_width > OLED_WIDTH)
        draw_width = OLED_WIDTH - cursor_col;
    if (cursor_row + draw_height > OLED_HEIGHT)
        draw_height = OLED_HEIGHT - cursor_row;

    uint8_t image_bytes_per_row = (image_width + 7) / 8;

    uint8_t start_page = cursor_row >> 3;
    uint8_t end_page = (cursor_row + draw_height - 1) >> 3;

    uint8_t column_data[OLED_WIDTH];

    for (uint8_t page = start_page; page <= end_page && page < 8; page++)
    {
        uint8_t page_start_display_row = page * 8;

        for (uint8_t c = 0; c < draw_width; c++)
        {
            uint8_t column_byte = 0;

            for (uint8_t bit = 0; bit < 8; bit++)
            {
                uint8_t display_row = page_start_display_row + bit;

                if (display_row >= cursor_row && display_row < cursor_row + draw_height)
                {
                    uint8_t image_row = display_row - cursor_row;

                    if (image_row < image_height)
                    {
                        uint8_t image_col = c;
                        if (image_col < image_width)
                        {
                            uint8_t byte_index = image_col / 8;
                            uint8_t bit_in_byte = 7 - (image_col % 8); // MSB = left

                            uint16_t data_offset = (uint16_t)image_row * image_bytes_per_row + byte_index;

                            if (data[data_offset] & (1 << bit_in_byte))
                            {
                                column_byte |= (1 << bit);
                            }
                        }
                    }
                }
            }

            column_data[c] = column_byte;
        }

        if (framebuffer_mutex != NULL)
        {
            if (xSemaphoreTake(framebuffer_mutex, portMAX_DELAY) == pdTRUE)
            {
                for (uint8_t c = 0; c < draw_width && (cursor_col + c) < OLED_WIDTH; c++)
                {
                    framebuffer[page][cursor_col + c] = column_data[c];
                }
                xSemaphoreGive(framebuffer_mutex);
            }
        }
    }
}

void oled_update_display()
{
    oled_update_display_partial(0, OLED_WIDTH - 1, 0, FRAMEBUFFER_PAGES - 1);
}

void oled_update_display_partial(uint8_t start_col, uint8_t end_col, uint8_t start_page, uint8_t end_page)
{
    if (start_col > OLED_WIDTH - 1)
        start_col = OLED_WIDTH - 1;
    if (end_col > OLED_WIDTH - 1)
        end_col = OLED_WIDTH - 1;
    if (start_col > end_col)
        return; // Invalid range

    if (start_page > FRAMEBUFFER_PAGES - 1)
        start_page = FRAMEBUFFER_PAGES - 1;
    if (end_page > FRAMEBUFFER_PAGES - 1)
        end_page = FRAMEBUFFER_PAGES - 1;
    if (start_page > end_page)
        return; // Invalid range

    uint8_t num_cols = end_col - start_col + 1;

    if (xSemaphoreTake(framebuffer_mutex, portMAX_DELAY) == pdTRUE)
    {
        if (xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
        {
            oled_set_memory_addressing_mode(0); // Horizontal addressing mode
            oled_set_column_address(start_col, end_col);
            oled_set_page_address(start_page, end_page);

            for (uint8_t page = start_page; page <= end_page; page++)
            {
                oled_write_impl(&framebuffer[page][start_col], num_cols, false);
            }

            xSemaphoreGive(display_mutex);
        }
        xSemaphoreGive(framebuffer_mutex);
    }
}

void oled_scroll_line(oled_scroll_dir_t direction)
{
    if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
    {
        if (direction == SCROLL_VERTICAL_DOWN) // Scroll down
        {
            current_first_line = (current_first_line + 1) % OLED_HEIGHT;
        }
        else // Scroll up
        {
            current_first_line = (current_first_line == 0) ? OLED_HEIGHT - 1 : current_first_line - 1;
        }

        uint8_t start_line_cmd = OLED_SET_START_LINE | (current_first_line & 0x3F);
        uint8_t cmd[] = {start_line_cmd};
        oled_write(cmd, true);

        xSemaphoreGive(display_mutex);
    }
}

static void oled_scroll_task(void *pvParameters)
{
    scroll_task_params_t *params = (scroll_task_params_t *)pvParameters;
    if (params == NULL)
        return;

    // Copy params to local variables immediately
    oled_scroll_dir_t initial_dir = params->direction;
    uint32_t initial_speed_ms = params->speed_ms;

    // Use global variables so direction can be changed dynamically
    while (1)
    {
        // Check if we should stop (scroll_type set to SCROLL_NONE)
        if (scroll_type == SCROLL_NONE)
        {
            break;
        }

        // Use current scroll_type and saved_speed_ms (can be updated by oled_scroll_vertical)
        oled_scroll_dir_t dir = scroll_type;
        uint32_t speed_ms = saved_speed_ms;
        if (speed_ms < 1)
            speed_ms = initial_speed_ms; // Fallback to initial speed

        oled_scroll_line(dir);
        vTaskDelay(pdMS_TO_TICKS(speed_ms));
    }

    // Clear handle before exiting (oled_scroll_software_off will free params)
    scroll_task_handle = NULL;
    vTaskDelete(NULL);
}

static void oled_scroll_hardware_off(void);
static void oled_scroll_software_off(void);
void oled_scroll_off(void);

void oled_scroll_horizontal(oled_scroll_dir_t direction, uint32_t speed_ms, uint8_t start_page, uint8_t end_page)
{
    oled_scroll_hardware_off();

    if (speed_ms < 1)
        speed_ms = 1;
    else if (speed_ms > 1000)
        speed_ms = 1000;

    start_page &= 0x07;
    end_page &= 0x07;
    if (end_page < start_page)
        end_page = start_page;

    if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
    {
        scroll_type = direction;
        saved_start_page = start_page;
        saved_end_page = end_page;

        bool h_dir = (direction == SCROLL_HORIZONTAL_RIGHT);
        uint8_t scroll_cmd = h_dir ? OLED_RIGHT_HORIZONTAL_SCROLL : OLED_LEFT_HORIZONTAL_SCROLL;
        uint8_t scroll_setup[] = {
            scroll_cmd,
            0x00,              // A[7:0] - Dummy byte
            start_page & 0x07, // B[2:0] - Start page address
            0x07,              // C[2:0] - Scroll interval (111b = 2 frames, fastest)
            end_page & 0x07,   // D[2:0] - End page address
            0x00,              // E[7:0] - Dummy byte
            0xFF               // F[7:0] - Dummy byte
        };
        oled_write(scroll_setup, true);

        uint8_t activate_cmd[] = {OLED_ACTIVATE_SCROLL};
        oled_write(activate_cmd, true);

        xSemaphoreGive(display_mutex);
    }
}

void oled_scroll_vertical(oled_scroll_dir_t direction, uint32_t speed_ms)
{
    if (speed_ms < 1)
        speed_ms = 1;
    else if (speed_ms > 1000)
        speed_ms = 1000;

    scroll_type = direction;
    saved_speed_ms = speed_ms;

    if (scroll_task_handle == NULL)
    {
        scroll_task_params = (scroll_task_params_t *)malloc(sizeof(scroll_task_params_t));
        if (scroll_task_params == NULL)
        {
            ESP_LOGE("ssd1306", "Failed to allocate scroll task params");
            return;
        }

        scroll_task_params->direction = direction;
        scroll_task_params->speed_ms = speed_ms;

        xTaskCreate(oled_scroll_task, "oled_scroll", 4096, scroll_task_params, 5, &scroll_task_handle);
    }
}

void oled_scroll_diagonal(oled_scroll_dir_t v_direction, oled_scroll_dir_t h_direction, uint32_t speed_ms, uint8_t vertical_offset)
{
    oled_scroll_hardware_off();
    oled_scroll_software_off();
    scroll_type = SCROLL_NONE;

    oled_scroll_horizontal(h_direction, speed_ms, 0, 7);
    oled_scroll_vertical(v_direction, speed_ms);
}

static void oled_scroll_hardware_off(void)
{
    if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
    {
        uint8_t deactivate_cmd[] = {OLED_DEACTIVATE_SCROLL};
        oled_write(deactivate_cmd, true);
        xSemaphoreGive(display_mutex);
    }
}

static void oled_scroll_software_off(void)
{
    if (scroll_task_handle != NULL)
    {
        // Signal task to exit by setting scroll_type to SCROLL_NONE
        scroll_type = SCROLL_NONE;

        // Wait a bit for task to exit cleanly (it will set scroll_task_handle to NULL)
        vTaskDelay(pdMS_TO_TICKS(50));

        // If task still exists, force delete it
        if (scroll_task_handle != NULL)
        {
            TaskHandle_t task_to_delete = scroll_task_handle;
            scroll_task_handle = NULL;
            vTaskDelete(task_to_delete);
        }

        // Free params (task does not free them to avoid double-free)
        if (scroll_task_params != NULL)
        {
            free(scroll_task_params);
            scroll_task_params = NULL;
        }
    }
}

void oled_scroll_off(void)
{
    oled_scroll_hardware_off();
    oled_scroll_software_off();
    scroll_type = SCROLL_NONE;
}

void oled_init(i2c_master_dev_handle_t dev)
{
    oled_dev = dev;

    const uint8_t init_seq[] = {
        0x00,

        OLED_DISPLAY_OFF,
        OLED_SET_DISPLAY_CLOCK_DIV, 0x80,
        OLED_SET_MULTIPLEX_RATIO, 0x3F,
        OLED_SET_DISPLAY_OFFSET, 0x00,
        OLED_SET_START_LINE | 0x00,
        OLED_CHARGE_PUMP, 0x14,
        OLED_SET_MEMORY_MODE, 0x00,
        OLED_SET_SEGMENT_REMAP_1,
        OLED_SET_COM_SCAN_DEC,
        OLED_SET_COM_PINS, 0x12,
        OLED_SET_CONTRAST, 0x20,
        OLED_ENTIRE_DISPLAY_RESUME,
        OLED_NORMAL_DISPLAY,
        OLED_DEACTIVATE_SCROLL,
        OLED_DISPLAY_ON};

    i2c_master_transmit(dev, init_seq, sizeof(init_seq), -1);

    framebuffer_mutex = xSemaphoreCreateMutex();
    display_mutex = xSemaphoreCreateMutex();
    memset(framebuffer, 0, sizeof(framebuffer));
    oled_clear_display();
    oled_update_display();
}
