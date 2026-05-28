#ifndef GPIO_DRIVER_H
#define GPIO_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/types.h"

typedef enum
{
    GPIO_DRIVER_IRQ_EDGE_RISE = 1 << 0,
    GPIO_DRIVER_IRQ_EDGE_FALL = 1 << 1,
    GPIO_DRIVER_IRQ_LEVEL_HIGH = 1 << 2,
    GPIO_DRIVER_IRQ_LEVEL_LOW = 1 << 3

} gpio_driver_irq_event_t;

typedef void (*gpio_driver_callback_t)(uint  gpio, uint32_t events, void *user_data);

typedef struct
{
    uint  gpio;

    bool direction_out;

    bool pull_up;
    bool pull_down;

    gpio_driver_callback_t callback;
    void *user_data;

} gpio_driver_config_t;

void gpio_driver_init(const gpio_driver_config_t *config);

void gpio_driver_write(uint  gpio, bool state);

bool gpio_driver_read(uint  gpio);

void gpio_driver_enable_irq(uint  gpio, uint32_t events);

void gpio_driver_disable_irq(uint  gpio, uint32_t events);

#endif // GPIO_DRIVER_H