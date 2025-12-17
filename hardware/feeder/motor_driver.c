#include "motor_driver.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "motor_driver";

static gpio_num_t motor_in1 = GPIO_NUM_NC;
static gpio_num_t motor_in2 = GPIO_NUM_NC;
static gpio_num_t motor_in3 = GPIO_NUM_NC;
static gpio_num_t motor_in4 = GPIO_NUM_NC;

static const uint8_t step_sequence[8] = {
    0b1000, // Step 1: IN1 HIGH
    0b1100, // Step 2: IN1 + IN2 HIGH
    0b0100, // Step 3: IN2 HIGH
    0b0110, // Step 4: IN2 + IN3 HIGH
    0b0010, // Step 5: IN3 HIGH
    0b0011, // Step 6: IN3 + IN4 HIGH
    0b0001, // Step 7: IN4 HIGH
    0b1001  // Step 8: IN4 + IN1 HIGH
};

static void set_motor_step(uint8_t step_pattern)
{
    gpio_set_level(motor_in1, (step_pattern & 0b1000) ? 1 : 0);
    gpio_set_level(motor_in2, (step_pattern & 0b0100) ? 1 : 0);
    gpio_set_level(motor_in3, (step_pattern & 0b0010) ? 1 : 0);
    gpio_set_level(motor_in4, (step_pattern & 0b0001) ? 1 : 0);
}

static void motor_stop(void)
{
    set_motor_step(0b0000);
}

static void motor_rotate_steps(int steps)
{
    if (steps == 0)
    {
        return;
    }

    int direction = (steps > 0) ? 1 : -1;
    int abs_steps = (steps > 0) ? steps : -steps;
    int current_step = 0;

    for (int i = 0; i < abs_steps; i++)
    {
        set_motor_step(step_sequence[current_step]);
        esp_rom_delay_us(2000); // 2ms

        current_step += direction;
        if (current_step >= 8)
        {
            current_step = 0;
        }
        else if (current_step < 0)
        {
            current_step = 7;
        }
    }

    motor_stop();
}

void motor_rotate_portion(bool direction)
{
    int steps = direction ? STEPS_PER_PORTION : -STEPS_PER_PORTION;
    motor_rotate_steps(steps);
}

void motor_driver_init(gpio_num_t in1, gpio_num_t in2, gpio_num_t in3, gpio_num_t in4)
{
    motor_in1 = in1;
    motor_in2 = in2;
    motor_in3 = in3;
    motor_in4 = in4;

    gpio_config_t io_conf = {
        .pin_bit_mask = ((1ULL << in1) |
                         (1ULL << in2) |
                         (1ULL << in3) |
                         (1ULL << in4)),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    motor_stop();

    ESP_LOGI(TAG, "Motor driver initialized (GPIOs: %d, %d, %d, %d)",
             in1, in2, in3, in4);
}
