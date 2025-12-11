#ifndef PH_SENSOR_DRIVER_H
#define PH_SENSOR_DRIVER_H

#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"

// ADC channel assignments (these map to GPIO pins)
// GPIO 32 -> ADC_CHANNEL_4
// GPIO 33 -> ADC_CHANNEL_5

// Voltage divider compensation
// If using a voltage divider (e.g., 10kΩ/10kΩ to divide 5V to 2.5V), set to 2.0
// If connecting directly (sensor outputs 0-3.3V), set to 1.0
#define PH_VOLTAGE_DIVIDER_RATIO 1.0f // Change to 2.0f after adding voltage divider

// pH calculation formula: pH = 3.5 * voltage_volts + Offset
// Based on Arduino code: pHValue = 3.5*voltage+Offset
#define PH_SCALE_FACTOR 3.5f
#define PH_OFFSET 0.0f // Calibration offset (adjust as needed)

float ph_sensor_read_ph(void);
float ph_sensor_read_temp_comp_mv(void);
void ph_sensor_init(gpio_num_t ph_output_gpio, gpio_num_t temp_comp_gpio);

#endif // PH_SENSOR_DRIVER_H
