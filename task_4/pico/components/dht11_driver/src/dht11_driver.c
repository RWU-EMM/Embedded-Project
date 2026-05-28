#include "dht11_driver.h"

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define DHT11_START_SIGNAL_MS 20
#define DHT11_RESPONSE_TIMEOUT_US 100
#define DHT11_BIT_TIMEOUT_US 100

static bool wait_for_level(uint gpio,
                           bool level,
                           uint32_t timeout_us)
{
    absolute_time_t start = get_absolute_time();

    while (gpio_get(gpio) != level)
    {
        if (absolute_time_diff_us(start,
                                  get_absolute_time()) > timeout_us)
        {
            return false;
        }
    }

    return true;
}

static bool dht11_timer_callback(repeating_timer_t *rt)
{
    dht11_t *dht = (dht11_t *)rt->user_data;

    bool ok = dht11_read(dht);

    dht->data_valid = ok;

    return true;
}

bool dht11_init(dht11_t *dht,
                uint gpio)
{
    if (!dht)
    {
        return false;
    }

    dht->gpio = gpio;

    dht->temperature = 0.0f;
    dht->humidity = 0.0f;
    dht->data_valid = false;

    gpio_init(gpio);

    gpio_set_dir(gpio, GPIO_IN);

    gpio_pull_up(gpio);

    return true;
}

bool dht11_read(dht11_t *dht)
{
    if (!dht)
    {
        return false;
    }

    uint8_t data[5] = {0};

    // MCU START SIGNAL
    gpio_set_dir(dht->gpio, GPIO_OUT);

    gpio_put(dht->gpio, 0);

    sleep_ms(DHT11_START_SIGNAL_MS);

    gpio_put(dht->gpio, 1);

    sleep_us(30);

    gpio_set_dir(dht->gpio, GPIO_IN);

    // SENSOR RESPONSE
    if (!wait_for_level(dht->gpio,
                        0,
                        DHT11_RESPONSE_TIMEOUT_US))
    {
        return false;
    }

    if (!wait_for_level(dht->gpio, 1, DHT11_RESPONSE_TIMEOUT_US))
    {
        return false;
    }

    if (!wait_for_level(dht->gpio, 0, DHT11_RESPONSE_TIMEOUT_US))
    {
        return false;
    }

    // READ 40 BITS
    for (int i = 0; i < 40; i++)
    {
        // wait LOW
        if (!wait_for_level(dht->gpio,
                            1,
                            DHT11_BIT_TIMEOUT_US))
        {
            return false;
        }

        absolute_time_t start = get_absolute_time();

        // wait HIGH end
        if (!wait_for_level(dht->gpio,
                            0,
                            DHT11_BIT_TIMEOUT_US))
        {
            return false;
        }

        int pulse_width = absolute_time_diff_us(start, get_absolute_time());

        // DHT11:  ~26us => 0  ~70us => 1
        uint8_t bit =
            (pulse_width > 40) ? 1 : 0;

        data[i / 8] <<= 1;

        data[i / 8] |= bit;
    }

    uint8_t checksum =
        data[0] +
        data[1] +
        data[2] +
        data[3];

    if (checksum != data[4])
    {
        return false;
    }

    dht->humidity = (float)data[0];

    dht->temperature = (float)data[2];

    return true;
}

float dht11_get_temperature(dht11_t *dht)
{
    if (!dht)
    {
        return 0.0f;
    }

    return dht->temperature;
}

float dht11_get_humidity(dht11_t *dht)
{
    if (!dht)
    {
        return 0.0f;
    }

    return dht->humidity;
}

bool dht11_start_periodic_read(dht11_t *dht, uint32_t interval_ms)
{
    if (!dht)
    {
        return false;
    }

    return add_repeating_timer_ms(-interval_ms, dht11_timer_callback, dht,
                                  &dht->timer);
}

void dht11_stop_periodic_read(dht11_t *dht)
{
    if (!dht)
    {
        return;
    }

    cancel_repeating_timer(&dht->timer);
}

bool dht11_is_data_valid(dht11_t *dht)
{
    if (!dht)
    {
        return false;
    }

    return dht->data_valid;
}