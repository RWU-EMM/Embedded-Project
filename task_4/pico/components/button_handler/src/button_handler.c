#include "button_handler.h"
#include "gpio_driver.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "pico/cyw43_arch.h"

#define MAX_BUTTONS 4

typedef struct
{
    bool used;

    uint gpio;

    bool active_low;

    uint32_t debounce_ms;
    uint32_t long_press_ms;

    absolute_time_t last_irq_time;
    absolute_time_t press_time;

    bool pressed;

    button_callback_t callback;
    void *user_data;

} button_t;

static button_t buttons[MAX_BUTTONS];

static button_t *find_button(uint gpio)
{
    for (int i = 0; i < MAX_BUTTONS; i++)
    {
        if (buttons[i].used &&
            buttons[i].gpio == gpio)
        {
            return &buttons[i];
        }
    }

    return NULL;
}

static void button_gpio_callback(uint gpio, uint32_t events, void *user_data)
{
    (void)events;
    (void)user_data;

    button_t *button = find_button(gpio);

    if (!button)
    {
        return;
    }

    absolute_time_t now = get_absolute_time();

    uint32_t diff = absolute_time_diff_us(button->last_irq_time, now) / 1000;

    if (diff < button->debounce_ms)
    {
        return;
    }

    button->last_irq_time = now;

    bool raw_state = gpio_driver_read(gpio);

    bool pressed_state = button->active_low ? !raw_state : raw_state;

    if (pressed_state)
    {
        button->pressed = true;
        button->press_time = now;
    }
    else
    {
        if (button->pressed)
        {
            uint32_t press_duration = absolute_time_diff_us(button->press_time, now) / 1000;

            if (press_duration >= button->long_press_ms)
            {
                button->callback(gpio, BUTTON_EVENT_LONG_PRESS, button->user_data);
            }
            else
            {
                button->callback(gpio, BUTTON_EVENT_SHORT_PRESS, button->user_data);
            }
        }

        button->pressed = false;
    }
}

void button_handler_init(const button_config_t *config)
{
    for (int i = 0; i < MAX_BUTTONS; i++)
    {
        if (!buttons[i].used)
        {
            buttons[i].used = true;

            buttons[i].gpio = config->gpio;

            buttons[i].active_low = config->active_low;

            buttons[i].debounce_ms = config->debounce_ms;

            buttons[i].long_press_ms = config->long_press_ms;

            buttons[i].callback = config->callback;

            buttons[i].user_data = config->user_data;

            buttons[i].pressed = false;

            gpio_driver_config_t gpio_cfg = {
                .gpio = config->gpio,

                .direction_out = GPIO_IN,

                .pull_up = config->active_low,
                .pull_down = !config->active_low,

                .callback = button_gpio_callback,
                .user_data = NULL};

            gpio_driver_init(&gpio_cfg);

            gpio_driver_enable_irq(config->gpio, GPIO_DRIVER_IRQ_EDGE_RISE | GPIO_DRIVER_IRQ_EDGE_FALL);

            break;
        }
    }
}