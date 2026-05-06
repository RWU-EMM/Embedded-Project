#include <stdio.h>
#include "potentiometer_driver.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gptimer.h"

#include "esp_log.h"

#define DEFAULT_READ_PREIOD_MS 100 // 100ms

#define DEFAULT_ADC_IN_MIN 0
#define DEFAULT_ADC_IN_MAX 4095
#define DEFAULT_ADC_OUT_MIN 100
#define DEFAULT_ADC_OUT_MAX 1000


static int32_t map_value(int32_t x)
{
    if (x < DEFAULT_ADC_IN_MIN)
    {
        x = DEFAULT_ADC_IN_MIN;
    }

    if (x > DEFAULT_ADC_IN_MAX)
    {
        x = DEFAULT_ADC_IN_MAX;
    }

    return (((x - DEFAULT_ADC_IN_MIN) * (DEFAULT_ADC_OUT_MAX - DEFAULT_ADC_OUT_MIN)) /
                (DEFAULT_ADC_IN_MAX - DEFAULT_ADC_IN_MIN) +
            DEFAULT_ADC_OUT_MIN);
}

static bool IRAM_ATTR pot_timer_cb(gptimer_handle_t timer,
                                   const gptimer_alarm_event_data_t *edata,
                                   void *user_data)
{
    potentiometer_t *pot = (potentiometer_t *)user_data;

    int raw = 0;
    adc_oneshot_read(pot->unit_handler, pot->channel, &raw);

    pot->last_value = map_value(raw);

    if (pot->cb)
    {
        pot->cb(pot->last_value);
    }

    return false;
}

esp_err_t init_potentiometer(potentiometer_t *pot)
{

    // ADC config (oneshot mode)
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = pot->unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &pot->unit_handler));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(pot->unit_handler, pot->channel, &chan_cfg));

    // Timer config
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };

    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &pot->timer));

    gptimer_event_callbacks_t cbs = {
        .on_alarm = pot_timer_cb,
    };

    ESP_ERROR_CHECK(gptimer_register_event_callbacks(pot->timer, &cbs, pot));
    ESP_ERROR_CHECK(gptimer_enable(pot->timer));

    potentiometer_set_period(pot, DEFAULT_READ_PREIOD_MS);

    return ESP_OK;
}

esp_err_t potentiometer_set_period(potentiometer_t *pot, uint32_t period_ms)
{
    pot->sampling_period_us = period_ms * 1000;

    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = pot->sampling_period_us,
        .flags.auto_reload_on_alarm = 1,
    };

    return gptimer_set_alarm_action(pot->timer, &alarm_config);
}

esp_err_t potentiometer_start(potentiometer_t *pot)
{
    return gptimer_start(pot->timer);
}

esp_err_t potentiometer_stop(potentiometer_t *pot)
{
    return gptimer_stop(pot->timer);
}

uint32_t potentiometer_get_value(potentiometer_t *pot)
{
    return pot->last_value;
}