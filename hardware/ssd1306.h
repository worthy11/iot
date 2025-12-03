#ifndef SSD1306_H
#define SSD1306_H

#include "driver/i2c_master.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define oled_write_array(payload, is_command) \
    oled_write_impl(payload, sizeof(payload), is_command)

typedef enum
{
    SCROLL_NONE,
    SCROLL_HORIZONTAL_LEFT,
    SCROLL_HORIZONTAL_RIGHT,
    SCROLL_VERTICAL_UP,
    SCROLL_VERTICAL_DOWN,
} oled_scroll_dir_t;

void oled_init(i2c_master_dev_handle_t dev);

void oled_display_on(void);
void oled_display_off(void);
void oled_clear_display(void);
void oled_normal_display(void);
void oled_invert_display(void);
void oled_update_display();
void oled_update_display_partial(uint8_t start_col, uint8_t end_col, uint8_t start_page, uint8_t end_page);

void oled_flip_horizontal(bool flip);
void oled_flip_vertical(bool flip);

void oled_set_vertical_offset(uint8_t offset);
void oled_set_contrast(uint8_t contrast);
void oled_set_position(uint8_t x, uint8_t y);

void oled_draw_text(const char *str, uint8_t font_size, uint16_t rotation);
void oled_draw_text_inverse(const char *str, uint8_t font_size, uint16_t rotation);
void oled_draw_bitmap(const uint8_t *data, uint8_t image_width, uint8_t image_height);

void oled_scroll_horizontal(oled_scroll_dir_t direction, uint32_t speed_ms, uint8_t start_page, uint8_t end_page);
void oled_scroll_vertical(oled_scroll_dir_t direction, uint32_t speed_ms);
void oled_scroll_diagonal(oled_scroll_dir_t v_direction, oled_scroll_dir_t h_direction, uint32_t speed_ms, uint8_t vertical_offset);
void oled_scroll_line(oled_scroll_dir_t direction);
void oled_scroll_off(void);

#endif // SSD1306_H