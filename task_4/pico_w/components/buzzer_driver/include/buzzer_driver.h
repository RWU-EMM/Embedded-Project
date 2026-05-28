#ifndef BUZZER_DRIVER_H
#define BUZZER_DRIVER_H


#include <stdint.h>
#include <stdbool.h>
#include "pico/types.h"

typedef struct
{
    uint gpio;

    bool active_high;

} buzzer_config_t;

typedef struct
{
    uint gpio;

    bool active_high;

    bool initialized;

} buzzer_t;

bool buzzer_init(buzzer_t *buzzer,const buzzer_config_t *config);

void buzzer_on(buzzer_t *buzzer);

void buzzer_off(buzzer_t *buzzer);

void buzzer_toggle(buzzer_t *buzzer);

void buzzer_beep(buzzer_t *buzzer,uint32_t duration_ms);



#endif // BUZZER_DRIVER_H