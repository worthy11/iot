#include "temp_sensor_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "temp_sensor";
static gpio_num_t s_pin = GPIO_NUM_4; // default D4

// 1-Wire timing helpers
static inline void ow_delay_us(uint32_t us) { esp_rom_delay_us(us); }

static void ow_drive_low(void) {
	gpio_set_direction(s_pin, GPIO_MODE_OUTPUT);
	gpio_set_level(s_pin, 0);
}

static void ow_release(void) {
	gpio_set_direction(s_pin, GPIO_MODE_INPUT);
}

static int ow_read_level(void) { return gpio_get_level(s_pin); }

static bool ow_reset(void) {
    //wystawienie impulsu reset, czyli zwarciu linii danych na 480 μs do masy. 
	ow_drive_low();
	ow_delay_us(480);
    //Następnie każde urządzenie slave potwierdza swoją obecność 
	ow_release();
	ow_delay_us(70);
	int presence = !ow_read_level();
	ow_delay_us(410);
	return presence;
}

static void ow_write_bit(int bit) {
	ow_drive_low();
	if (bit) {
		ow_delay_us(6);
		ow_release();
		ow_delay_us(64);
	} else {
		ow_delay_us(60);
		ow_release();
		ow_delay_us(10);
	}
}

static int ow_read_bit(void) {
	int b;
	ow_drive_low();
	ow_delay_us(6);
	ow_release();
	ow_delay_us(9);
	b = ow_read_level();
	ow_delay_us(55);
	return b;
}

static void ow_write_byte(uint8_t v) {
	for (int i = 0; i < 8; i++) {
		ow_write_bit((v >> i) & 0x01);
	}
}

static uint8_t ow_read_byte(void) {
	uint8_t v = 0;
	for (int i = 0; i < 8; i++) {
		if (ow_read_bit()) v |= (1 << i);
	}
	return v;
}
static void temp_log_task(void *arg) {
	uint32_t interval_ms = (uint32_t)(uintptr_t)arg;
	float c;
	while (1) {
		if (temp_sensor_read_celsius(&c)) {
			ESP_LOGI(TAG, "Temperature: %.2f C", c);
		} else {
			ESP_LOGW(TAG, "Temperature read failed");
		}
		vTaskDelay(pdMS_TO_TICKS(interval_ms));
	}
}
void temp_sensor_init(gpio_num_t pin) {
	s_pin = pin;
	gpio_config_t cfg = {
		.pin_bit_mask = 1ULL << s_pin,
		.mode = GPIO_MODE_INPUT_OUTPUT_OD,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	gpio_config(&cfg);
	// Ensure line idle high via internal pull-up; external 4.7k recommended
	ow_release();
	bool present = ow_reset();
	ESP_LOGI(TAG, "DS18B20 presence: %s", present ? "yes" : "no");

    uint32_t interval_ms = 10000;
	xTaskCreate(temp_log_task, "temp_log", 2048, (void *)(uintptr_t)interval_ms, 5, NULL);
}

bool temp_sensor_read_celsius(float *out_celsius) {
	if (!out_celsius) return false;
	if (!ow_reset()) {
		ESP_LOGW(TAG, "No presence pulse");
		return false;
	}
	// Skip ROM (single device) then Convert T
	ow_write_byte(0xCC); // SKIP ROM
	ow_write_byte(0x44); // CONVERT T

	// DS18B20 max conversion time 750ms for 12-bit
	// Use polling: wait until bus goes high (parasitic power needs strong pull-up; we just delay)
	vTaskDelay(pdMS_TO_TICKS(750));

	if (!ow_reset()) {
		ESP_LOGW(TAG, "No presence after convert");
		return false;
	}
	ow_write_byte(0xCC); // SKIP ROM
	ow_write_byte(0xBE); // READ SCRATCHPAD

	uint8_t temp_l = ow_read_byte();
	uint8_t temp_h = ow_read_byte();

	int16_t raw = (int16_t)((temp_h << 8) | temp_l);
	// 12-bit resolution: each LSB = 0.0625°C
	*out_celsius = (float)raw * 0.0625f;
	// Read remaining bytes to complete transaction (optional)
	for (int i = 0; i < 7; i++) (void)ow_read_byte();
	return true;
}




