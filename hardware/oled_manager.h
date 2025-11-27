#include "driver/i2c_master.h"
#include <stdint.h>
#include <stdbool.h>

void oled_init(i2c_master_dev_handle_t dev);
void oled_print_text(const char *text, uint8_t row, uint8_t col, bool orientation);
void oled_scroll_line(bool direction);
