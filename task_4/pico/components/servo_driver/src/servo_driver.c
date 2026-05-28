#include "servo_driver.h"

#include "pico/stdlib.h"
#include "hardware/pwm.h"

#define SERVO_PWM_FREQ_HZ 50
#define SERVO_PWM_WRAP 20000

static servo_t servos[SERVO_MAX_COUNT];

/*
 * Helper:
 * configure PWM for servo
 */
static void servo_pwm_init(uint gpio)
{
    gpio_set_function(gpio, GPIO_FUNC_PWM);

    uint slice_num = pwm_gpio_to_slice_num(gpio);

    pwm_config config = pwm_get_default_config();

    /*
     * System clock = 125 MHz
     *
     * Divider = 125
     * PWM clock = 1 MHz
     *
     * Therefore:
     * 1 tick = 1 us
     */
    pwm_config_set_clkdiv(&config, 125.0f);

    /*
     * 20,000 ticks
     * => 20 ms period
     * => 50 Hz
     */
    pwm_config_set_wrap(&config, SERVO_PWM_WRAP - 1);

    pwm_init(slice_num, &config, true);
}

/*
 * Initialize driver
 */
void servo_driver_init(void)
{
    for (int i = 0; i < SERVO_MAX_COUNT; i++)
    {
        servos[i].is_attached = false;
    }
}

/*
 * Attach servo
 */
int servo_attach(uint8_t gpio)
{
    for (int i = 0; i < SERVO_MAX_COUNT; i++)
    {
        if (!servos[i].is_attached)
        {
            servo_pwm_init(gpio);

            servos[i].is_attached = true;
            servos[i].gpio = gpio;
            servos[i].slice_num = pwm_gpio_to_slice_num(gpio);
            servos[i].channel = pwm_gpio_to_channel(gpio);
            servos[i].current_angle = 0.0f;

            /*
             * Move to initial position
             */
            servo_write_angle(i, 0.0f);

            return i;
        }
    }

    return -1;
}

/*
 * Convert pulse width to PWM compare
 */
void servo_write_us(int handle, uint16_t pulse_us)
{
    if (handle < 0 || handle >= SERVO_MAX_COUNT)
    {
        return;
    }

    if (!servos[handle].is_attached)
    {
        return;
    }

    if (pulse_us < SERVO_MIN_PULSE_US)
    {
        pulse_us = SERVO_MIN_PULSE_US;
    }

    if (pulse_us > SERVO_MAX_PULSE_US)
    {
        pulse_us = SERVO_MAX_PULSE_US;
    }

    pwm_set_chan_level(servos[handle].slice_num, servos[handle].channel, pulse_us);
}

/*
 * Set angle
 */
void servo_write_angle(int handle, float angle)
{
    if (handle < 0 || handle >= SERVO_MAX_COUNT)
    {
        return;
    }

    if (!servos[handle].is_attached)
    {
        return;
    }

    if (angle < SERVO_MIN_ANGLE)
    {
        angle = SERVO_MIN_ANGLE;
    }

    if (angle > SERVO_MAX_ANGLE)
    {
        angle = SERVO_MAX_ANGLE;
    }

    servos[handle].current_angle = angle;

    /*
     * Map:
     * 0°   -> 1000 us
     * 180° -> 2000 us
     */
    float pulse = SERVO_MIN_PULSE_US + ((angle / 180.0f) * (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US));

    servo_write_us(handle, (uint16_t)pulse);
}

/*
 * Disable servo
 */
void servo_detach(int handle)
{
    if (handle < 0 || handle >= SERVO_MAX_COUNT)
    {
        return;
    }

    if (!servos[handle].is_attached)
    {
        return;
    }

    pwm_set_enabled(servos[handle].slice_num, false);

    servos[handle].is_attached = false;
}