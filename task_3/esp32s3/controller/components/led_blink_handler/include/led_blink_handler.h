#ifndef LED_BLINK_HANDLER_H
#define LED_BLINK_HANDLER_H

#include <stdint.h>
#include "driver/gpio.h"
#include "driver/gptimer.h"

#include "esp_err.h"
 

typedef struct
{
    gpio_num_t gpio_num;
    gptimer_handle_t timer;
    uint8_t state; // current state of led HIGH:1 , LOW:0
    uint8_t ctrl_en; // enable

} led_blink_t;

esp_err_t init_led_blink(led_blink_t *led, gpio_pull_mode_t mode);
esp_err_t led_blink_set_frequency(led_blink_t *led, float freq_hz);
esp_err_t led_blink_start(led_blink_t *led);
esp_err_t led_blink_stop(led_blink_t *led);
uint8_t get_timer_run_state();

#endif // LED_BLINK_HANDLER_H