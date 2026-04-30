#ifndef POTENTIOMETER_DRIVER_H
#define POTENTIOMETER_DRIVER_H

#include <stdint.h>
#include "driver/gptimer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"


typedef void (*pot_cb_t)(uint32_t value);

typedef struct
{
    adc_unit_t unit;
    adc_channel_t channel;
    adc_oneshot_unit_handle_t unit_handler;
    gptimer_handle_t timer;

    uint32_t sampling_period_us;
    uint32_t last_value;

    pot_cb_t cb;

} potentiometer_t;


esp_err_t init_potentiometer(potentiometer_t *pot);
esp_err_t potentiometer_set_period(potentiometer_t *pot, uint32_t period_ms);
esp_err_t potentiometer_start(potentiometer_t *pot);
esp_err_t potentiometer_stop(potentiometer_t *pot);
uint32_t potentiometer_get_value(potentiometer_t *pot);


#endif // POTENTIOMETER_DRIVER_H