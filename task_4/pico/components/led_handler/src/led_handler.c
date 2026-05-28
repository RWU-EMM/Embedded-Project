#include "led_handler.h"

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

static inline void led_write(led_t *led, bool value);

static inline void led_write(led_t *led, bool value)
{
    if (!led->en_ctrl) // turn off if disabled
    {
        if (led->hw_type == _LED_HW_CYW43)
        {
            cyw43_arch_gpio_put(led->pin_num, _LED_OFF_STATE);
        }
        else
        {
            gpio_put(led->pin_num, _LED_OFF_STATE);
        }
        return;
    }

    if (led->hw_type == _LED_HW_CYW43)
    {
        cyw43_arch_gpio_put(led->pin_num, value);
    }
    else
    {
        gpio_put(led->pin_num, value);
    }
}

// Internal callback
static bool led_timer_callback(struct repeating_timer *t)
{
    led_t *led = (led_t *)t->user_data;

    if (led->mode != _LED_MODE_BLINK)
    {
        return false;
    }

    if (!led->en_ctrl)
    {
        return true; // stay alive but do nothing
    }

    // Toggle state
    led->state = (led->state == _LED_ON_STATE) ? _LED_OFF_STATE : _LED_ON_STATE;

    led_write(led, led->state);

    return true;
}

// Init
void init_led(led_t *led)
{

    if (led->hw_type == _LED_HW_GPIO)
    {
        gpio_init(led->pin_num);
        gpio_set_dir(led->pin_num, GPIO_OUT);
    }

    led->state = _LED_OFF_STATE;
    led->mode = _LED_MODE_OFF;
    led->timer_active = false;

    led_write(led, 0);
}

// Deinit
void deinit_led(led_t *led)
{
    if (led->timer_active)
    {
        cancel_repeating_timer(&led->timer);
        led->timer_active = false;
    }

    led_write(led, 0);

    if (led->hw_type == _LED_HW_GPIO)
    {
        gpio_deinit(led->pin_num);
    }
}

// Start blinking
void led_start_blink(led_t *led, int interval_ms)
{
    if(!led->en_ctrl)
    {
        led_stop_blink(led);
        led_off(led);
        return; // Don't start if disabled
    }
    if (led->timer_active)
    {
        cancel_repeating_timer(&led->timer);
    }

    led->blink_interval_ms = interval_ms;
    led->mode = _LED_MODE_BLINK;

    add_repeating_timer_ms(
        interval_ms,
        led_timer_callback,
        led,
        &led->timer);

    led->timer_active = true;
}

// Stop blinking
void led_stop_blink(led_t *led)
{
    if (led->timer_active)
    {
        cancel_repeating_timer(&led->timer);
        led->timer_active = false;
    }

    led->mode = _LED_MODE_OFF;
    led_write(led, 0);
}

// Change interval
void led_set_interval(led_t *led, int interval_ms)
{
    led->blink_interval_ms = interval_ms;

    if (led->mode == _LED_MODE_BLINK)
    {
        led_start_blink(led, interval_ms);
    }
}

// Turn ON
void led_on(led_t *led)
{
    if (led->timer_active)
    {
        cancel_repeating_timer(&led->timer);
        led->timer_active = false;
    }

    led->mode = _LED_MODE_ON;
    led->state = _LED_ON_STATE;

    led_write(led, 1);
}

// Turn OFF
void led_off(led_t *led)
{
    if (led->timer_active)
    {
        cancel_repeating_timer(&led->timer);
        led->timer_active = false;
    }

    led->mode = _LED_MODE_OFF;
    led->state = _LED_OFF_STATE;

    led_write(led, 0);
}
