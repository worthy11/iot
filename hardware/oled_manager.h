#include "driver/i2c_master.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define oled_write_array(payload, is_command) \
    oled_write_impl(payload, sizeof(payload), is_command)

void oled_init(i2c_master_dev_handle_t dev);
void oled_display_on(void);
void oled_display_off(void);
void oled_clear_display(void);
void oled_normal_display(void);
void oled_invert_display(void);
void oled_set_contrast(uint8_t contrast);
void oled_set_position(uint8_t row, uint8_t col);
void oled_draw_char(char c, uint8_t font_size, uint16_t rotation);
void oled_draw_string(const char *str, uint8_t font_size, uint16_t rotation);
void oled_update_screen(uint8_t display_id);

void oled_scroll_horizontal(bool direction, uint8_t start_page, uint8_t end_page);
void oled_scroll_diagonal(bool direction, uint8_t start_page, uint8_t end_page, uint8_t vertical_offset);
void oled_scroll_off(void);

void oled_rotate_90(bool enable);
void oled_draw_image(const uint8_t *image_data, uint8_t row, uint8_t col, uint8_t width, uint8_t height);
