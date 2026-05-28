#include "gpio_driver.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define MAX_GPIO_PINS 30

typedef struct
{
    gpio_driver_callback_t callback;
    void *user_data;

} gpio_callback_entry_t;

static gpio_callback_entry_t gpio_callbacks[MAX_GPIO_PINS];

static void internal_gpio_irq_handler(uint gpio, uint32_t events)
{
    if (gpio < MAX_GPIO_PINS)
    {
        if (gpio_callbacks[gpio].callback)
        {
            gpio_callbacks[gpio].callback(gpio, events, gpio_callbacks[gpio].user_data);
        }
    }
}

void gpio_driver_init(const gpio_driver_config_t *config)
{
    gpio_init(config->gpio);

    gpio_set_dir(config->gpio, config->direction_out ? GPIO_OUT : GPIO_IN);

    if (config->pull_up)
    {
        gpio_pull_up(config->gpio);
    }

    if (config->pull_down)
    {
        gpio_pull_down(config->gpio);
    }

    gpio_callbacks[config->gpio].callback = config->callback;

    gpio_callbacks[config->gpio].user_data = config->user_data;
}

void gpio_driver_write(uint  gpio, bool state)
{
    gpio_put(gpio, state);
}

bool gpio_driver_read(uint  gpio)
{
    return gpio_get(gpio);
}

void gpio_driver_enable_irq(uint  gpio, uint32_t events)
{
    uint32_t pico_events = 0;

    if (events & GPIO_DRIVER_IRQ_EDGE_RISE)
    {
        pico_events |= GPIO_IRQ_EDGE_RISE;
    }

    if (events & GPIO_DRIVER_IRQ_EDGE_FALL)
    {
        pico_events |= GPIO_IRQ_EDGE_FALL;
    }

    if (events & GPIO_DRIVER_IRQ_LEVEL_HIGH)
    {
        pico_events |= GPIO_IRQ_LEVEL_HIGH;
    }

    if (events & GPIO_DRIVER_IRQ_LEVEL_LOW)
    {
        pico_events |= GPIO_IRQ_LEVEL_LOW;
    }

    gpio_set_irq_enabled_with_callback(gpio, pico_events, true, &internal_gpio_irq_handler);
}

void gpio_driver_disable_irq(uint  gpio, uint32_t events)
{
    uint32_t pico_events = 0;

    if (events & GPIO_DRIVER_IRQ_EDGE_RISE)
    {
        pico_events |= GPIO_IRQ_EDGE_RISE;
    }

    if (events & GPIO_DRIVER_IRQ_EDGE_FALL)
    {
        pico_events |= GPIO_IRQ_EDGE_FALL;
    }

    if (events & GPIO_DRIVER_IRQ_LEVEL_HIGH)
    {
        pico_events |= GPIO_IRQ_LEVEL_HIGH;
    }

    if (events & GPIO_DRIVER_IRQ_LEVEL_LOW)
    {
        pico_events |= GPIO_IRQ_LEVEL_LOW;
    }

    gpio_set_irq_enabled(gpio, pico_events, false);
}