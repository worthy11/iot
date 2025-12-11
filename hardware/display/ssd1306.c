#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "ssd1306.h"

#define OLED_SET_CONTRAST 0x81
#define OLED_ENTIRE_DISPLAY_ON 0xA5
#define OLED_ENTIRE_DISPLAY_RESUME 0xA4
#define OLED_NORMAL_DISPLAY 0xA6
#define OLED_INVERT_DISPLAY 0xA7
#define OLED_DISPLAY_OFF 0xAE
#define OLED_DISPLAY_ON 0xAF

#define OLED_DEACTIVATE_SCROLL 0x2E
#define OLED_ACTIVATE_SCROLL 0x2F
#define OLED_SET_VERTICAL_SCROLL_AREA 0xA3
#define OLED_RIGHT_HORIZONTAL_SCROLL 0x26
#define OLED_LEFT_HORIZONTAL_SCROLL 0x27
#define OLED_VERTICAL_RIGHT_SCROLL 0x29
#define OLED_VERTICAL_LEFT_SCROLL 0x2A

#define OLED_SET_MEMORY_MODE 0x20
#define OLED_SET_COLUMN_ADDR 0x21
#define OLED_SET_PAGE_ADDR 0x22
#define OLED_SET_PAGE_START_ADDR 0xB0

#define OLED_SET_START_LINE 0x40
#define OLED_SET_SEGMENT_REMAP_0 0xA0
#define OLED_SET_SEGMENT_REMAP_1 0xA1
#define OLED_SET_MULTIPLEX_RATIO 0xA8
#define OLED_SET_COM_SCAN_INC 0xC0
#define OLED_SET_COM_SCAN_DEC 0xC8
#define OLED_SET_DISPLAY_OFFSET 0xD3
#define OLED_SET_COM_PINS 0xDA

#define OLED_SET_DISPLAY_CLOCK_DIV 0xD5
#define OLED_SET_PRECHARGE 0xD9
#define OLED_SET_VCOM_DETECT 0xDB

#define OLED_CHARGE_PUMP 0x8D

#define OLED_RMW_START 0xE0
#define OLED_RMW_END 0xEE

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define FRAMEBUFFER_PAGES (OLED_HEIGHT / 8)

static const char font[128][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0000 (nul)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0001
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0002
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0003
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0004
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0005
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0006
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0007
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0008
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0009
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000A
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000B
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000C
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000D
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000E
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+000F
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0010
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0011
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0012
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0013
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0014
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0015
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0016
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0017
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0018
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0019
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001A
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001B
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001C
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001D
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001E
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+001F
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0020 (space)
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, // U+0021 (!)
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0022 (")
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00}, // U+0023 (#)
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, // U+0024 ($)
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00}, // U+0025 (%)
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, // U+0026 (&)
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0027 (')
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, // U+0028 (()
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00}, // U+0029 ())
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, // U+002A (*)
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00}, // U+002B (+)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // U+002C (,)
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00}, // U+002D (-)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // U+002E (.)
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00}, // U+002F (/)
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00}, // U+0030 (0)
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00}, // U+0031 (1)
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, // U+0032 (2)
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00}, // U+0033 (3)
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, // U+0034 (4)
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00}, // U+0035 (5)
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, // U+0036 (6)
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00}, // U+0037 (7)
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // U+0038 (8)
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00}, // U+0039 (9)
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // U+003A (:)
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // U+003B (;)
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00}, // U+003C (<)
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00}, // U+003D (=)
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00}, // U+003E (>)
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00}, // U+003F (?)
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, // U+0040 (@)
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00}, // U+0041 (A)
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00}, // U+0042 (B)
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00}, // U+0043 (C)
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00}, // U+0044 (D)
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00}, // U+0045 (E)
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00}, // U+0046 (F)
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00}, // U+0047 (G)
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00}, // U+0048 (H)
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+0049 (I)
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00}, // U+004A (J)
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00}, // U+004B (K)
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00}, // U+004C (L)
    {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00}, // U+004D (M)
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00}, // U+004E (N)
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00}, // U+004F (O)
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00}, // U+0050 (P)
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00}, // U+0051 (Q)
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00}, // U+0052 (R)
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00}, // U+0053 (S)
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+0054 (T)
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00}, // U+0055 (U)
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // U+0056 (V)
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // U+0057 (W)
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00}, // U+0058 (X)
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00}, // U+0059 (Y)
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00}, // U+005A (Z)
    {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00}, // U+005B ([)
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00}, // U+005C (\)
    {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00}, // U+005D (])
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00}, // U+005E (^)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, // U+005F (_)
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0060 (`)
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00}, // U+0061 (a)
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00}, // U+0062 (b)
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00}, // U+0063 (c)
    {0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00}, // U+0064 (d)
    {0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00}, // U+0065 (e)
    {0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00}, // U+0066 (f)
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // U+0067 (g)
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00}, // U+0068 (h)
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+0069 (i)
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E}, // U+006A (j)
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00}, // U+006B (k)
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+006C (l)
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00}, // U+006D (m)
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00}, // U+006E (n)
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00}, // U+006F (o)
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F}, // U+0070 (p)
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78}, // U+0071 (q)
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00}, // U+0072 (r)
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00}, // U+0073 (s)
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00}, // U+0074 (t)
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00}, // U+0075 (u)
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // U+0076 (v)
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00}, // U+0077 (w)
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00}, // U+0078 (x)
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // U+0079 (y)
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00}, // U+007A (z)
    {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00}, // U+007B ({)
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, // U+007C (|)
    {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00}, // U+007D (})
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+007E (~)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}  // U+007F
};

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
static volatile uint8_t saved_fps = 0;

typedef struct
{
    oled_scroll_dir_t direction;
    uint8_t fps;
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
        uint8_t cmd[] = {flip ? OLED_SET_SEGMENT_REMAP_0 : OLED_SET_SEGMENT_REMAP_1};
        oled_write(cmd, true);
        xSemaphoreGive(display_mutex);
    }
}

void oled_flip_vertical(bool flip)
{
    if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
    {
        uint8_t cmd[] = {flip ? OLED_SET_COM_SCAN_INC : OLED_SET_COM_SCAN_DEC};
        oled_write(cmd, true);
        xSemaphoreGive(display_mutex);
    }
}

void oled_set_vertical_offset(uint8_t offset)
{
    if (display_mutex != NULL && xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
    {
        uint8_t start_line_cmd = OLED_SET_START_LINE | (offset & 0x3F);
        uint8_t cmd[] = {start_line_cmd};
        oled_write(cmd, true);

        xSemaphoreGive(display_mutex);
    }
}

static void oled_set_memory_addressing_mode(uint8_t mode)
{
    mode &= 0x03;
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
        const uint8_t *font_data = (const uint8_t *)font[char_code];
        oled_draw_char(*str, font_size, rotation, font_data);
        str++;
    }
    cursor_row = temp_row;
    cursor_col = temp_col;
}

static void oled_draw_char_inverse(char c, uint8_t font_size, uint16_t rotation)
{
    uint8_t char_code = (uint8_t)c & 0x7F;
    const uint8_t *original_font = (const uint8_t *)font[char_code];

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
                            uint8_t bit_in_byte = 7 - (image_col % 8);

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
        return;

    if (start_page > FRAMEBUFFER_PAGES - 1)
        start_page = FRAMEBUFFER_PAGES - 1;
    if (end_page > FRAMEBUFFER_PAGES - 1)
        end_page = FRAMEBUFFER_PAGES - 1;
    if (start_page > end_page)
        return;

    uint8_t num_cols = end_col - start_col + 1;

    if (xSemaphoreTake(framebuffer_mutex, portMAX_DELAY) == pdTRUE)
    {
        if (xSemaphoreTake(display_mutex, portMAX_DELAY) == pdTRUE)
        {
            oled_set_memory_addressing_mode(0);
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
        if (direction == SCROLL_VERTICAL_DOWN)
        {
            current_first_line = (current_first_line + 1) % OLED_HEIGHT;
        }
        else
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

    while (1)
    {
        if (scroll_type == SCROLL_NONE)
        {
            break;
        }

        oled_scroll_dir_t dir = scroll_type;
        uint8_t fps = saved_fps;
        if (fps < 1)
            fps = params->fps;

        uint32_t delay_ms = (fps > 0) ? (1000 / fps) : 1000;
        if (delay_ms < 1)
            delay_ms = 1;

        oled_scroll_line(dir);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    scroll_task_handle = NULL;
    vTaskDelete(NULL);
}

static void oled_scroll_hardware_off(void);
static void oled_scroll_software_off(void);
void oled_scroll_off(void);

void oled_scroll_horizontal(oled_scroll_dir_t direction, uint8_t fps, uint8_t start_page, uint8_t end_page)
{
    oled_scroll_hardware_off();

    if (fps < 1)
        fps = 1;
    else if (fps > 60)
        fps = 60;

    start_page &= 0x07;
    end_page &= 0x07;
    if (end_page < start_page)
        end_page = start_page;

    // 000b=5 frames, 001b=64, 010b=128, 011b=256, 100b=3, 101b=4, 110b=25, 111b=2
    uint8_t scroll_interval;
    if (fps >= 30)
        scroll_interval = 0x07; // 2 frames (~30 FPS)
    else if (fps >= 20)
        scroll_interval = 0x04; // 3 frames (~20 FPS)
    else if (fps >= 15)
        scroll_interval = 0x05; // 4 frames (~15 FPS)
    else if (fps >= 12)
        scroll_interval = 0x00; // 5 frames (~12 FPS)
    else if (fps >= 3)
        scroll_interval = 0x06; // 25 frames (~2.4 FPS)
    else
        scroll_interval = 0x01; // 64 frames (~0.94 FPS)

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
            scroll_interval,   // C[2:0] - Scroll interval (based on fps)
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

void oled_scroll_vertical(oled_scroll_dir_t direction, uint8_t fps)
{
    if (fps < 1)
        fps = 1;
    else if (fps > 60)
        fps = 60;

    scroll_type = direction;
    saved_fps = fps;

    if (scroll_task_handle == NULL)
    {
        scroll_task_params = (scroll_task_params_t *)malloc(sizeof(scroll_task_params_t));
        if (scroll_task_params == NULL)
        {
            return;
        }

        scroll_task_params->direction = direction;
        scroll_task_params->fps = fps;

        xTaskCreate(oled_scroll_task, "oled_scroll", 4096, scroll_task_params, 5, &scroll_task_handle);
    }
}

void oled_scroll_diagonal(oled_scroll_dir_t v_direction, oled_scroll_dir_t h_direction, uint8_t v_fps, uint8_t h_fps)
{
    oled_scroll_hardware_off();
    oled_scroll_software_off();
    scroll_type = SCROLL_NONE;

    if (v_fps < 1)
        v_fps = 1;
    else if (v_fps > 60)
        v_fps = 60;

    if (h_fps < 1)
        h_fps = 1;
    else if (h_fps > 60)
        h_fps = 60;

    oled_scroll_horizontal(h_direction, h_fps, 0, 7);
    oled_scroll_vertical(v_direction, v_fps);
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
        scroll_type = SCROLL_NONE;
        vTaskDelay(pdMS_TO_TICKS(50));

        if (scroll_task_handle != NULL)
        {
            TaskHandle_t task_to_delete = scroll_task_handle;
            scroll_task_handle = NULL;
            vTaskDelete(task_to_delete);
        }

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
