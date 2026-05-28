#ifndef LED_HANDLER_H
#define LED_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/time.h"

typedef enum
{
    _LED_OFF_STATE = 0,
    _LED_ON_STATE = 1,
} led_state_e;

typedef enum
{
    _LED_MODE_OFF = 0,
    _LED_MODE_ON,
    _LED_MODE_BLINK
} led_mode_e;

typedef enum
{
    _LED_HW_GPIO = 0,
    _LED_HW_CYW43
} led_hw_type_e;

typedef struct
{
    uint8_t pin_num;
    uint8_t en_ctrl;
    led_hw_type_e hw_type;
    led_state_e state;
    int blink_interval_ms;
    led_mode_e mode;
    struct repeating_timer timer;
    bool timer_active;
} led_t;

// API
void init_led(led_t *led);
void deinit_led(led_t *led);

void led_start_blink(led_t *led, int interval_ms);
void led_stop_blink(led_t *led);

void led_set_interval(led_t *led, int interval_ms);

void led_on(led_t *led);
void led_off(led_t *led);

#endif // LED_HANDLER_H