#ifndef BUTTON_HANDLER_H
#define BUTTON_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/types.h"

typedef enum
{
    BUTTON_EVENT_SHORT_PRESS,
    BUTTON_EVENT_LONG_PRESS

} button_event_t;

typedef void (*button_callback_t)(uint gpio, button_event_t event, void *user_data);

typedef struct
{
    uint gpio;

    bool active_low;

    uint32_t debounce_ms;
    uint32_t long_press_ms;

    button_callback_t callback;
    void *user_data;

} button_config_t;

void button_handler_init(const button_config_t *config);

#endif // BUTTON_HANDLER_H