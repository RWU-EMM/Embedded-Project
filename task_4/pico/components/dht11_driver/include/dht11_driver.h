#ifndef DHT11_DRIVER_H
#define DHT11_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

#include "pico/types.h"
#include "pico/time.h"

typedef struct
{
    uint gpio;

    float temperature;

    float humidity;

    bool data_valid;

    repeating_timer_t timer;

} dht11_t;

bool dht11_init(dht11_t *dht,uint gpio);

bool dht11_read(dht11_t *dht);

bool dht11_start_periodic_read(dht11_t *dht,uint32_t interval_ms);

void dht11_stop_periodic_read(dht11_t *dht);

float dht11_get_temperature(dht11_t *dht);

float dht11_get_humidity(dht11_t *dht);

bool dht11_is_data_valid(dht11_t *dht);

#endif