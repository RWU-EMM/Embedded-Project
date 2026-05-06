#include "rgb_led.h"

#include "ws2812.pio.h"

static inline uint32_t pack_color(rgb_led_t *dev,
                                  uint8_t r, uint8_t g, uint8_t b
#if RGB_LED_USE_RGBW
                                  ,
                                  uint8_t w
#endif
)
{
    uint32_t out = 0;

    switch (dev->order)
    {
    case RGB_ORDER_GRB:
#if RGB_LED_USE_RGBW
        out = ((uint32_t)g << 16) |
              ((uint32_t)r << 8) |
              ((uint32_t)w << 24) |
              b;
#else
        out = ((uint32_t)g << 16) |
              ((uint32_t)r << 8) |
              b;
#endif
        break;

    case RGB_ORDER_RGB:
#if RGB_LED_USE_RGBW
        out = ((uint32_t)r << 16) |
              ((uint32_t)g << 8) |
              ((uint32_t)w << 24) |
              b;
#else
        out = ((uint32_t)r << 16) |
              ((uint32_t)g << 8) |
              b;
#endif
        break;

    case RGB_ORDER_BRG:
#if RGB_LED_USE_RGBW
        out = ((uint32_t)b << 16) |
              ((uint32_t)r << 8) |
              ((uint32_t)w << 24) |
              g;
#else
        out = ((uint32_t)b << 16) |
              ((uint32_t)r << 8) |
              g;
#endif
        break;
    }

    return out;
}

void init_rgb_led(rgb_led_t *dev)
{

    bool ok = pio_claim_free_sm_and_add_program_for_gpio_range(
        &ws2812_program,
        &dev->pio,
        &dev->sm,
        &dev->offset,
        dev->pin,
        1,
        true);

    hard_assert(ok);

    ws2812_program_init(
        dev->pio,
        dev->sm,
        dev->offset,
        dev->pin,
        800000,
        RGB_LED_USE_RGBW);

    // ===== DMA INIT =====
    dev->dma_chan = dma_claim_unused_channel(true);

    dma_channel_config c = dma_channel_get_default_config(dev->dma_chan);

    // 32-bit transfers
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);

    // increment read (memory)
    channel_config_set_read_increment(&c, true);

    // no increment write (PIO FIFO)
    channel_config_set_write_increment(&c, false);

    // connect to PIO TX FIFO
    channel_config_set_dreq(&c, pio_get_dreq(dev->pio, dev->sm, true));

    dma_channel_configure(
        dev->dma_chan,
        &c,
        &dev->pio->txf[dev->sm], // write addr
        NULL,                    // read addr (set later)
        0,                       // count
        false                    // don't start yet
    );

    rgb_led_clear(dev);
    // rgb_led_update(dev);
}

void rgb_led_clear(rgb_led_t *dev)
{
    for (int i = 0; i < dev->led_count; i++)
    {
        dev->pixels[i].state = false;
        dev->pixels[i].mode = RGB_LED_MODE_OFF;
    }
}

void  rgb_led_update(rgb_led_t *dev)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());

    for (int i = 0; i < dev->led_count; i++)
    {
        rgb_pixel_t *p = &dev->pixels[i];
        uint32_t color = 0;

        switch (p->mode)
        {
        case RGB_LED_MODE_OFF:
            color = 0;
            break;

        case RGB_LED_MODE_ON:
            p->state = true;
#if RGB_LED_USE_RGBW
            color = pack_color(dev, p->r, p->g, p->b, p->w);
#else
            color = pack_color(dev, p->r, p->g, p->b);
#endif
            break;

        case RGB_LED_MODE_BLINK:
            if (now - p->last_toggle >= p->blink_interval_ms)
            {
                p->state = !p->state;
                p->last_toggle = now;
            }

            if (p->state)
            {
#if RGB_LED_USE_RGBW
                color = pack_color(dev, p->r, p->g, p->b, p->w);
#else
                color = pack_color(dev, p->r, p->g, p->b);
#endif
            }
            else
            {
                color = 0;
            }
            break;
        }

        // pio_sm_put_blocking(dev->pio, dev->sm, color << 8u);
        dev->frame_buf[i] = color << 8u;
    }
    rgb_led_show(dev);
}

void rgb_led_off_pixel(rgb_led_t *dev, int idx)
{
    if (idx < 0 || idx >= dev->led_count)
        return;

    dev->pixels[idx].mode = RGB_LED_MODE_OFF;
}

void rgb_led_on_pixel(rgb_led_t *dev, int idx,
                      uint8_t r, uint8_t g, uint8_t b
#if RGB_LED_USE_RGBW
                      ,
                      uint8_t w
#endif
)
{
    if (idx < 0 || idx >= dev->led_count)
        return;

    rgb_pixel_t *p = &dev->pixels[idx];

    p->r = r;
    p->g = g;
    p->b = b;
#if RGB_LED_USE_RGBW
    p->w = w;
#endif

    p->mode = RGB_LED_MODE_ON;
    p->state = true;
}

void rgb_led_blink_pixel(rgb_led_t *dev, int idx,
                         uint8_t r, uint8_t g, uint8_t b,
#if RGB_LED_USE_RGBW
                         uint8_t w,
#endif
                         int interval_ms)
{
    if (idx < 0 || idx >= dev->led_count)
        return;

    if (interval_ms <= 0)
        interval_ms = 100; // default

    rgb_pixel_t *p = &dev->pixels[idx];

    p->r = r;
    p->g = g;
    p->b = b;
#if RGB_LED_USE_RGBW
    p->w = w;
#endif

    p->mode = RGB_LED_MODE_BLINK;
    p->blink_interval_ms = interval_ms;
    p->last_toggle = to_ms_since_boot(get_absolute_time());
    p->state = true;
}

void rgb_led_show(rgb_led_t *dev)
{
    dma_channel_wait_for_finish_blocking(dev->dma_chan);

    dma_channel_set_read_addr(dev->dma_chan, dev->frame_buf, false);
    dma_channel_set_trans_count(dev->dma_chan, dev->led_count, true);

    // WS2812 reset time (~300us)
    sleep_us(300);
}