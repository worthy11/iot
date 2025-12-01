#include "oled_commands.h"
#include "oled_manager.h"
#include "font8x8_basic.h"
#include "fish_image.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static i2c_master_dev_handle_t oled_dev = NULL;
static bool display_rotated = false;
static uint8_t cursor_col = 0;
static uint8_t cursor_row = 0;

typedef enum
{
    SCROLL_NONE,
    SCROLL_HORIZONTAL,
    SCROLL_DIAGONAL
} scroll_type_t;

static bool scroll_active = false;
static scroll_type_t scroll_type = SCROLL_NONE;
static bool scroll_direction = false;
static uint8_t scroll_start_page = 0;
static uint8_t scroll_end_page = 0;
static uint8_t scroll_vertical_offset = 0;

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

static void oled_restore_scroll(void)
{
    if (scroll_active)
    {
        if (scroll_type == SCROLL_HORIZONTAL)
        {
            oled_scroll_horizontal(scroll_direction, scroll_start_page, scroll_end_page);
        }
        else if (scroll_type == SCROLL_DIAGONAL)
        {
            oled_scroll_diagonal(scroll_direction, scroll_start_page, scroll_end_page, scroll_vertical_offset);
        }
    }
}

void oled_display_on(void)
{
    uint8_t cmd[] = {OLED_DISPLAY_ON};
    oled_write(cmd, true);
}

void oled_display_off(void)
{
    uint8_t cmd[] = {OLED_DISPLAY_OFF};
    oled_write(cmd, true);
}

void oled_clear_display()
{
    bool was_scrolling = scroll_active;
    oled_scroll_off();

    for (uint8_t page = 0; page < OLED_HEIGHT >> 3; page++)
    {
        oled_set_page_address(page, page);
        oled_set_column_address(0, OLED_WIDTH - 1);
        uint8_t data_buffer[OLED_WIDTH];
        for (uint16_t x = 0; x < OLED_WIDTH; x++)
        {
            data_buffer[x] = 0x00;
        }
        oled_write_impl(data_buffer, OLED_WIDTH, false);
    }

    if (was_scrolling)
    {
        oled_restore_scroll();
    }
}

void oled_normal_display(void)
{
    uint8_t normal_cmd[] = {OLED_NORMAL_DISPLAY};
    oled_write(normal_cmd, true);
}

void oled_invert_display(void)
{
    uint8_t invert_cmd[] = {OLED_INVERT_DISPLAY};
    oled_write(invert_cmd, true);
}

void oled_set_contrast(uint8_t contrast)
{
    uint8_t cmd[] = {OLED_SET_CONTRAST, contrast};
    oled_write(cmd, true);
}

void oled_set_position(uint8_t row, uint8_t col)
{
    cursor_col = col & 0x7F;
    cursor_row = (row < OLED_HEIGHT) ? row : (OLED_HEIGHT - 1);
    uint8_t page = (cursor_row >> 3) & 0x07;

    oled_set_column_address(cursor_col, cursor_col);
    oled_set_page_address(page, page);
}

void oled_draw_char(char c, uint8_t font_size, uint16_t rotation)
{
    if (font_size < 1)
        font_size = 1;
    else if (font_size > 2)
        font_size = 2;

    rotation = rotation % 360;
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270)
        rotation = 0;

    uint8_t char_code = (uint8_t)c & 0x7F;
    uint8_t base_size = 8 * font_size;
    uint8_t char_width = base_size;
    uint8_t char_height = base_size;

    const uint8_t *font_data = (const uint8_t *)font8x8_basic[char_code];
    uint8_t start_page = (cursor_row >> 3) & 0x07;
    uint8_t num_pages = ((cursor_row + char_height - 1) >> 3) - start_page + 1;
    if (num_pages > 8)
        num_pages = 8;

    for (uint8_t page = 0; page < num_pages; page++)
    {
        uint8_t page_num = (start_page + page) & 0x07;

        uint8_t col_start = cursor_col;
        uint8_t col_end = (cursor_col + char_width - 1) & 0x7F;
        oled_set_column_address(col_start, col_end);
        oled_set_page_address(page_num, page_num);

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

        oled_write_impl(char_data, char_width, false);
    }

    if (rotation == 0)
    {
        if (cursor_col + char_width < OLED_WIDTH)
            cursor_col += char_width;
        else
        {
            cursor_col = 0;
            cursor_row = cursor_row < OLED_HEIGHT - char_height ? cursor_row + 1 : 0;
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

void oled_scroll_off(void);
void oled_scroll_horizontal(bool direction, uint8_t start_page, uint8_t end_page)
{
    oled_scroll_off();
    uint8_t scroll_cmd = direction ? OLED_RIGHT_HORIZONTAL_SCROLL : OLED_LEFT_HORIZONTAL_SCROLL;
    start_page &= 0x07; // Clamp to valid page range (0-7)
    end_page &= 0x07;   // Clamp to valid page range (0-7)

    uint8_t scroll_setup[] = {
        scroll_cmd,
        0x00,       // A[7:0] - Dummy byte
        start_page, // B[2:0] - Start page address
        0x07,       // C[2:0] - Scroll interval (111b = 2 frames, fastest)
        end_page,   // D[2:0] - End page address
        0x00,       // E[7:0] - Dummy byte
        0xFF        // F[7:0] - Dummy byte
    };
    oled_write(scroll_setup, true);

    uint8_t activate_cmd[] = {OLED_ACTIVATE_SCROLL};
    oled_write(activate_cmd, true);

    scroll_active = true;
    scroll_type = SCROLL_HORIZONTAL;
    scroll_direction = direction;
    scroll_start_page = start_page;
    scroll_end_page = end_page;
}

void oled_scroll_diagonal(bool direction, uint8_t start_page, uint8_t end_page, uint8_t vertical_offset)
{
    oled_scroll_off();

    start_page &= 0x07;
    end_page &= 0x07;
    vertical_offset &= 0x3F; // Clamp to 6 bits (0-63 rows)

    // Set vertical scroll area: fixed top = 0, scroll rows = HEIGHT, fixed bottom = 0
    // Based on Adafruit library pattern: SET_VERTICAL_SCROLL_AREA, 0x00, HEIGHT
    // Note: The third parameter (fixed bottom) is implicit/calculated, but datasheet shows 3 params
    uint8_t scroll_area[] = {
        OLED_SET_VERTICAL_SCROLL_AREA,
        0x00,       // Fixed rows at top (0)
        OLED_HEIGHT // Scroll rows (full display height)
        // Fixed rows at bottom = 0 (implicit, total = HEIGHT)
    };
    oled_write(scroll_area, true);

    // Diagonal scroll command sequence
    // Based on Adafruit library pattern:
    // VERTICAL_SCROLL_CMD, 0x00, start, 0x00, stop, vertical_offset, ACTIVATE_SCROLL
    uint8_t scroll_cmd = direction ? OLED_VERTICAL_RIGHT_SCROLL : OLED_VERTICAL_LEFT_SCROLL;
    uint8_t scroll_setup[] = {
        scroll_cmd,          // 29h or 2Ah - Command byte
        0x00,                // Dummy byte
        start_page,          // Start page address
        0x00,                // Time interval (0x00 in Adafruit)
        end_page,            // End page address
        vertical_offset,     // Vertical scrolling offset (rows, typically 0x01)
        OLED_ACTIVATE_SCROLL // Activate scroll (sent together with offset in Adafruit)
    };
    oled_write(scroll_setup, true);

    scroll_active = true;
    scroll_type = SCROLL_DIAGONAL;
    scroll_direction = direction;
    scroll_start_page = start_page;
    scroll_end_page = end_page;
    scroll_vertical_offset = vertical_offset;
}

void oled_draw_string(const char *str, uint8_t font_size, uint16_t rotation)
{
    bool was_scrolling = scroll_active;
    if (was_scrolling)
    {
        oled_scroll_off();
    }

    while (*str != '\0')
    {
        oled_draw_char(*str, font_size, rotation);
        str++;
    }

    if (was_scrolling)
    {
        oled_restore_scroll();
    }
}

void oled_scroll_off(void)
{
    uint8_t deactivate_cmd[] = {OLED_DEACTIVATE_SCROLL};
    oled_write(deactivate_cmd, true);
    scroll_active = false;
    scroll_type = SCROLL_NONE;
}

void oled_draw_image(const uint8_t *image_data, uint8_t row, uint8_t col, uint8_t width, uint8_t height)
{
    bool was_scrolling = scroll_active;
    oled_scroll_off();

    uint8_t num_pages = OLED_HEIGHT >> 3; // 8 pages for 64px height

    for (uint8_t page = 0; page < num_pages; page++)
    {
        oled_set_page_address(page, page);
        oled_set_column_address(0, OLED_WIDTH - 1);
        uint8_t data_offset = page * OLED_WIDTH;
        oled_write_impl(&image_data[data_offset], OLED_WIDTH, false);
    }

    if (was_scrolling)
    {
        oled_restore_scroll();
    }
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

    uint8_t font_size = 1;
    while (1)
    {
        font_size = 1;
        oled_clear_display();
        oled_set_position(0, 0);
        oled_draw_string("AquaTest", font_size, 0);
        oled_set_position(OLED_HEIGHT - font_size * 8, OLED_WIDTH - font_size * 8);
        oled_draw_string("AquaTest", font_size, 180);
        vTaskDelay(pdMS_TO_TICKS(2000));

        font_size = 2;
        oled_clear_display();
        oled_set_position(0, 0);
        oled_draw_string("AquaTest", font_size, 0);
        oled_set_position(OLED_HEIGHT - font_size * 8, OLED_WIDTH - font_size * 8);
        oled_draw_string("AquaTest", font_size, 180);
        vTaskDelay(pdMS_TO_TICKS(2000));

        font_size = 1;
        oled_clear_display();
        oled_set_position(0, OLED_WIDTH - font_size * 8);
        oled_draw_string("AquaTest", font_size, 90);
        oled_set_position(OLED_HEIGHT - font_size * 8, 0);
        oled_draw_string("AquaTest", font_size, 270);
        vTaskDelay(pdMS_TO_TICKS(2000));

        font_size = 2;
        oled_clear_display();
        oled_set_position(0, OLED_WIDTH - font_size * 8);
        oled_draw_string("AquaTest", font_size, 90);
        oled_set_position(OLED_HEIGHT - font_size * 8, 0);
        oled_draw_string("AquaTest", font_size, 270);
        vTaskDelay(pdMS_TO_TICKS(2000));

        oled_scroll_diagonal(1, 0, 7, 1);
        vTaskDelay(pdMS_TO_TICKS(4000));
        oled_scroll_diagonal(0, 0, 7, 1);
        vTaskDelay(pdMS_TO_TICKS(4000));
    }
}
