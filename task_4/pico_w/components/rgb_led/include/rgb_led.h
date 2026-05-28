#ifndef RGB_LED_H
#define RGB_LED_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/time.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"

#define RGB_LED_MAX_PIXELS 10

// ===== CONFIG =====
#ifndef RGB_LED_USE_RGBW
#define RGB_LED_USE_RGBW 0
#endif

typedef enum
{
    RGB_ORDER_GRB = 0,
    RGB_ORDER_RGB,
    RGB_ORDER_BRG
} rgb_order_t;

typedef enum
{
    RGB_LED_MODE_OFF = 0,
    RGB_LED_MODE_ON,
    RGB_LED_MODE_BLINK
} rgb_mode_t;

typedef struct
{
    uint8_t r, g, b;
#if RGB_LED_USE_RGBW
    uint8_t w;
#endif

    int blink_interval_ms;
    uint32_t last_toggle;

    bool state; // ON/OFF state for blinking

    rgb_mode_t mode;

} rgb_pixel_t;

typedef struct
{
    uint8_t pin;
    uint8_t led_count;

    PIO pio;
    uint sm;
    uint offset;
    rgb_order_t order;
    rgb_pixel_t pixels[RGB_LED_MAX_PIXELS];
    uint32_t frame_buf[RGB_LED_MAX_PIXELS];
    int dma_chan;

} rgb_led_t;

// API
void init_rgb_led(rgb_led_t *dev);
void rgb_led_update(rgb_led_t *dev);
void rgb_led_clear(rgb_led_t *dev);
void rgb_led_off_pixel(rgb_led_t *dev, int idx);
void rgb_led_on_pixel(rgb_led_t *dev, int idx,
                      uint8_t r, uint8_t g, uint8_t b
#if RGB_LED_USE_RGBW
                      ,
                      uint8_t w
#endif
);

void rgb_led_blink_pixel(rgb_led_t *dev, int idx,
                         uint8_t r, uint8_t g, uint8_t b,
#if RGB_LED_USE_RGBW
                         uint8_t w,
#endif
                         int interval_ms);

void rgb_led_show(rgb_led_t *dev);

#endif