#ifndef AQUARIUM_DATA_H
#define AQUARIUM_DATA_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

typedef struct
{
    float temperature;
    float ph;
    time_t last_feed_time;
    time_t last_temp_measurement_time;
    bool last_feed_success;
    time_t next_feed_time;
    uint32_t temp_reading_interval_sec;
    uint32_t feeding_interval_sec;
    uint8_t display_contrast;
    uint8_t font_size;
    uint8_t line_height;
    bool temperature_display_enabled;
    bool ph_display_enabled;
    bool last_feeding_display_enabled;
    bool next_feeding_display_enabled;
    uint32_t display_sleep_time_min; // 0 means never
} aquarium_data_t;

void aquarium_data_init(void);
void aquarium_data_get(aquarium_data_t *data);
void aquarium_data_update_temperature(float temp);
void aquarium_data_update_ph(float ph);

void aquarium_data_update_last_feed(time_t feed_time, bool success);
void aquarium_data_update_next_feed(time_t next_time);

void aquarium_data_set_contrast(uint8_t contrast);
uint8_t aquarium_data_get_contrast(void);
void aquarium_data_set_font_size(uint8_t font_size);
uint8_t aquarium_data_get_font_size(void);
void aquarium_data_set_line_height(uint8_t line_height);
uint8_t aquarium_data_get_line_height(void);
void aquarium_data_set_display_enabled(bool temp, bool ph, bool last_feed, bool next_feed);

void aquarium_data_set_temp_reading_interval(uint32_t interval_sec);
uint32_t aquarium_data_get_temp_reading_interval(void);
void aquarium_data_set_feeding_interval(uint32_t interval_sec);
uint32_t aquarium_data_get_feeding_interval(void);

time_t aquarium_data_get_last_temp_measurement_time(void);
time_t aquarium_data_get_last_feed_time(void);

void aquarium_data_set_display_sleep_time(uint32_t sleep_time_min);
uint32_t aquarium_data_get_display_sleep_time(void);

#endif // AQUARIUM_DATA_H
