#include "oled_commands.h"
#include "oled_manager.h"
#include "font8x8_basic.h"
#include <string.h>

static i2c_master_dev_handle_t oled_dev = NULL;

static void oled_set_column_address(uint8_t start, uint8_t end)
{
    uint8_t cmd[] = {
        0x00,
        OLED_SET_COLUMN_ADDR, start, end};
    i2c_master_transmit(oled_dev, cmd, sizeof(cmd), -1);
}

static void oled_set_page_address(uint8_t start, uint8_t end)
{
    uint8_t cmd[] = {
        0x00,
        OLED_SET_PAGE_ADDR, start, end};
    i2c_master_transmit(oled_dev, cmd, sizeof(cmd), -1);
}

void oled_clear(uint8_t color)
{
    if (oled_dev == NULL)
        return;

    // Send RMWSTART command
    uint8_t rmw_start_cmd[] = {0x00, OLED_RMW_START};
    i2c_master_transmit(oled_dev, rmw_start_cmd, sizeof(rmw_start_cmd), -1);

    // CS(0) - Command mode (0x00 prefix)
    // Iterate over all pages
    for (uint8_t page = 0; page < ((OLED_HEIGHT - 1) >> 3) + 1; page++)
    {
        oled_set_page_address(page, page);
        oled_set_column_address(0, OLED_WIDTH - 1);

        // Prepare data buffer: 0x40 (data mode) + OLED_WIDTH bytes
        uint8_t data_buffer[OLED_WIDTH + 1];
        data_buffer[0] = 0x40; // Data mode

        // Iterate over OLED_WIDTH and write bytes using color arg
        for (uint16_t x = 0; x < OLED_WIDTH; x++)
        {
            data_buffer[x + 1] = color;
        }

        // Write data
        i2c_master_transmit(oled_dev, data_buffer, sizeof(data_buffer), -1);
    }

    // CS(1) - End data mode marker
    // (Data mode is implicit, but can send explicit command if needed)

    // Send RMWEND command
    uint8_t rmw_end_cmd[] = {0x00, OLED_RMW_END};
    i2c_master_transmit(oled_dev, rmw_end_cmd, sizeof(rmw_end_cmd), -1);
}

void oled_print_text(const char *text, uint8_t row, uint8_t col, bool vertical)
{
    if (oled_dev == NULL || text == NULL)
    {
        return;
    }

    uint8_t page = row;
    size_t text_len = strlen(text);

    for (size_t i = 0; i < text_len; i++)
    {
        uint8_t char_code = (uint8_t)text[i];
        if (char_code > 127)
            char_code = 0;

        uint8_t char_col = col + (i * 8);
        if (char_col > 127)
            break;

        uint8_t col_start = char_col;
        uint8_t col_end = char_col + 7;
        if (col_end > 127)
            col_end = 127;

        uint8_t page_start = page;
        uint8_t page_end = page;

        oled_set_column_address(col_start, col_end);
        oled_set_page_address(page_start, page_end);

        const uint8_t *font_data = (const uint8_t *)font8x8_basic[char_code];

        uint8_t char_data[9];
        char_data[0] = 0x40;

        // Font format: font_data[row] = horizontal row, MSB is leftmost pixel
        // Display format: each byte = vertical column, MSB is top pixel
        // Current issue: 90deg left rotation + horizontal flip
        // Solution: transpose and reverse column order to fix both issues
        for (int col = 0; col < 8; col++)
        {
            uint8_t column_byte = 0;
            // Read from column (7-col) to reverse horizontal direction
            int src_col = vertical ? col : (7 - col);

            for (int row = 0; row < 8; row++)
            {
                // Extract bit from font_data[row] at column src_col
                // Place it at bit position (7-row) in the column byte
                if (font_data[row] & (1 << (7 - src_col)))
                {
                    column_byte |= (1 << (7 - row));
                }
            }
            char_data[col + 1] = column_byte;
        }

        i2c_master_transmit(oled_dev, char_data, sizeof(char_data), -1);
    }
}

void oled_scroll_line(bool direction) {};

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
        OLED_SET_CONTRAST, 0x7F,
        OLED_ENTIRE_DISPLAY_RESUME,
        OLED_NORMAL_DISPLAY,
        OLED_DEACTIVATE_SCROLL,
        OLED_DISPLAY_ON};

    i2c_master_transmit(dev, init_seq, sizeof(init_seq), -1);
    oled_clear(0);
    oled_print_text("Hello World!", 0, 0, false);
}
