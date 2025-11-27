#include "oled_commands.h"
#include "oled_manager.h"

void oled_init(i2c_master_dev_handle_t dev)
{
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
}