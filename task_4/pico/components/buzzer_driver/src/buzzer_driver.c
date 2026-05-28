#include "buzzer_driver.h"

#include "gpio_driver.h"

#include "pico/stdlib.h"

bool buzzer_init(buzzer_t *buzzer, const buzzer_config_t *config)
{
    if (!buzzer || !config)
    {
        return false;
    }

    buzzer->gpio = config->gpio;

    buzzer->active_high = config->active_high;

    buzzer->initialized = true;

    gpio_driver_config_t gpio_cfg = {
        .gpio = config->gpio,

        .direction_out = true,

        .pull_up = false,

        .pull_down = false,

        .callback = NULL,

        .user_data = NULL};

    gpio_driver_init(&gpio_cfg);

    buzzer_off(buzzer);

    return true;
}

void buzzer_on(buzzer_t *buzzer)
{
    if (!buzzer || !buzzer->initialized)
    {
        return;
    }

    gpio_driver_write(buzzer->gpio, buzzer->active_high ? true : false);
}

void buzzer_off(buzzer_t *buzzer)
{
    if (!buzzer || !buzzer->initialized)
    {
        return;
    }

    gpio_driver_write(buzzer->gpio, buzzer->active_high ? false : true);
}

void buzzer_toggle(buzzer_t *buzzer)
{
    if (!buzzer || !buzzer->initialized)
    {
        return;
    }

    bool current =
        gpio_driver_read(buzzer->gpio);

    gpio_driver_write(buzzer->gpio, !current);
}

void buzzer_beep(buzzer_t *buzzer, uint32_t duration_ms)
{
    if (!buzzer || !buzzer->initialized)
    {
        return;
    }

    buzzer_on(buzzer);

    sleep_ms(duration_ms);

    buzzer_off(buzzer);
}