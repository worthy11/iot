#include "ph_sensor_driver.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static const char *TAG = "ph_sensor";
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t ph_output_cali_handle = NULL;
static adc_cali_handle_t temp_comp_cali_handle = NULL;
#define INVALID_ADC_CHANNEL 0xFF
static adc_channel_t ph_output_channel = INVALID_ADC_CHANNEL;
static adc_channel_t temp_comp_channel = INVALID_ADC_CHANNEL;
static bool adc_initialized = false;

// Helper function to convert GPIO to ADC1 channel
static adc_channel_t gpio_to_adc1_channel(gpio_num_t gpio)
{
    // ESP32 ADC1 channel mapping
    // GPIO 36 -> ADC1_CHANNEL_0, GPIO 37 -> ADC1_CHANNEL_1, GPIO 38 -> ADC1_CHANNEL_2, GPIO 39 -> ADC1_CHANNEL_3
    // GPIO 32 -> ADC1_CHANNEL_4, GPIO 33 -> ADC1_CHANNEL_5, GPIO 34 -> ADC1_CHANNEL_6, GPIO 35 -> ADC1_CHANNEL_7
    if (gpio >= 36 && gpio <= 39)
    {
        return (adc_channel_t)(gpio - 36); // GPIO 36-39 map to channels 0-3
    }
    else if (gpio >= 32 && gpio <= 35)
    {
        return (adc_channel_t)(gpio - 32 + 4); // GPIO 32-35 map to channels 4-7
    }
    ESP_LOGE(TAG, "Invalid GPIO %d for ADC1", gpio);
    return INVALID_ADC_CHANNEL;
}

static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated)
    {
        ESP_LOGI(TAG, "Calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated)
    {
        ESP_LOGI(TAG, "Calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "ADC calibration successful");
    }
    else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated)
    {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    }
    else
    {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

static void ph_sensor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "pH sensor task started");

    while (1)
    {
        float ph_value = ph_sensor_read_ph();
        float temp_comp_mv = ph_sensor_read_temp_comp_mv();

        // Note: ADC readings are logged in ph_sensor_read_ph() at DEBUG level
        ESP_LOGI(TAG, "pH: %.2f, Temp comp: %.1f mV", ph_value, temp_comp_mv);

        vTaskDelay(pdMS_TO_TICKS(1000)); // Read every 1 second
    }
}

void ph_sensor_init(gpio_num_t ph_output_gpio, gpio_num_t temp_comp_gpio)
{
    if (adc_initialized)
    {
        return;
    }

    // Convert GPIO to ADC channels
    ph_output_channel = gpio_to_adc1_channel(ph_output_gpio);
    temp_comp_channel = gpio_to_adc1_channel(temp_comp_gpio);

    if (ph_output_channel == INVALID_ADC_CHANNEL || temp_comp_channel == INVALID_ADC_CHANNEL)
    {
        ESP_LOGE(TAG, "Invalid GPIO pins for ADC1");
        return;
    }

    //-------------ADC1 Init---------------//
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_11, // 0-3.3V range
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ph_output_channel, &config));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, temp_comp_channel, &config));

    //-------------ADC1 Calibration Init---------------//
    adc_calibration_init(ADC_UNIT_1, ph_output_channel, ADC_ATTEN_DB_11, &ph_output_cali_handle);
    adc_calibration_init(ADC_UNIT_1, temp_comp_channel, ADC_ATTEN_DB_11, &temp_comp_cali_handle);

    adc_initialized = true;

    // Create task to read pH sensor every 1 second
    xTaskCreate(
        ph_sensor_task,
        "ph_sensor_task",
        2048,
        NULL,
        5,
        NULL);

    ESP_LOGI(TAG, "pH sensor driver initialized (GPIO %d: pH output, GPIO %d: temp comp)",
             ph_output_gpio, temp_comp_gpio);
}

float ph_sensor_read_temp_comp_mv(void)
{
    if (!adc_initialized)
    {
        ESP_LOGW(TAG, "ADC not initialized, call ph_sensor_init() first");
        return 0.0f;
    }

    int adc_reading = 0;
    int voltage_mv = 0;

    // Read ADC value
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, temp_comp_channel, &adc_reading));

    // Convert to millivolts
    if (temp_comp_cali_handle)
    {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(temp_comp_cali_handle, adc_reading, &voltage_mv));
    }
    else
    {
        // Fallback: approximate conversion (3.3V / 4095 * reading * 1000)
        voltage_mv = (adc_reading * 3300) / 4095;
    }

    return (float)voltage_mv;
}

float ph_sensor_read_ph(void)
{
    if (!adc_initialized)
    {
        ESP_LOGW(TAG, "ADC not initialized, call ph_sensor_init() first");
        return 0.0f;
    }

    int adc_reading = 0;
    int voltage_mv = 0;

    // Read ADC value for pH output
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ph_output_channel, &adc_reading));

    // Convert to millivolts
    if (ph_output_cali_handle)
    {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(ph_output_cali_handle, adc_reading, &voltage_mv));
    }
    else
    {
        // Fallback: approximate conversion (3.3V / 4095 * reading * 1000)
        voltage_mv = (adc_reading * 3300) / 4095;
    }

    // Convert millivolts to volts
    float voltage_volts = (float)voltage_mv / 1000.0f;

    // Compensate for voltage divider (if used)
    // If using 10kΩ/10kΩ divider, multiply by 2 to get original sensor voltage
    float sensor_voltage_volts = voltage_volts * PH_VOLTAGE_DIVIDER_RATIO;

    // Calculate pH: pH = 3.5 * voltage_volts + Offset
    // Based on Arduino code: pHValue = 3.5*voltage+Offset
    float ph_value = PH_SCALE_FACTOR * sensor_voltage_volts + PH_OFFSET;

    ESP_LOGI(TAG, "pH ADC: %d/4095, Measured: %d mV (%.3f V), Sensor: %.3f V, pH: %.2f",
             adc_reading, voltage_mv, voltage_volts, sensor_voltage_volts, ph_value);

    return ph_value;
}
