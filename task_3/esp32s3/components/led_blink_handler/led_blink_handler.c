#include <stdio.h>
#include "led_blink_handler.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"

#define DEFAULT_LED_EN 1 // defualt led on

#define DEFAULT_BLINK_FREQ_HZ 1.0f // [float] Hz

static uint8_t timer_run_state = 0;

static bool IRAM_ATTR led_timer_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data)
{
    led_blink_t *led = (led_blink_t *)user_data;

    if (led->ctrl_en)
    {
        led->state = !led->state;
    }
    else
    {
        // ELD's Are Active low
        led->state = 1; // off
    }

    gpio_set_level(led->gpio_num, led->state);

    return false;
}

esp_err_t init_led_blink(led_blink_t *led, gpio_pull_mode_t mode)
{

    led->state = 1;
    led->ctrl_en = DEFAULT_LED_EN;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << (led->gpio_num)),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE};

    ESP_ERROR_CHECK(gpio_config(&io_conf));

    ESP_ERROR_CHECK(gpio_set_pull_mode(led->gpio_num, mode));

    gpio_set_level(led->gpio_num, led->state);

    // Create timer
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1 MHz → 1 tick = 1 µs
    };

    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &led->timer));

    // Register callback
    gptimer_event_callbacks_t cbs = {
        .on_alarm = led_timer_cb,
    };

    ESP_ERROR_CHECK(gptimer_register_event_callbacks(led->timer, &cbs, led));

    // Enable timer
    ESP_ERROR_CHECK(gptimer_enable(led->timer));

    led_blink_set_frequency(led, DEFAULT_BLINK_FREQ_HZ);

    return ESP_OK;
}

esp_err_t led_blink_set_frequency(led_blink_t *led, float freq_hz)
{
    if (freq_hz > 20.0f)
        freq_hz = 20.0f;

    if (freq_hz < 0.5f)
        freq_hz = 0.5f;

    uint32_t period_us = (uint32_t)(1000000.0f / (freq_hz * 2));

    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = period_us,
        .flags.auto_reload_on_alarm = 1,
    };

    return gptimer_set_alarm_action(led->timer, &alarm_config);
}

esp_err_t led_blink_start(led_blink_t *led)
{
    if (led->blink_flag)
    {
        return ESP_OK;
    }
    led->blink_flag = 1;
    timer_run_state = 1;
    return gptimer_start(led->timer);
}

esp_err_t led_blink_stop(led_blink_t *led)
{
    led->blink_flag = 0;

    esp_err_t err = gptimer_stop(led->timer);

    led->state = 1; // OFF
    gpio_set_level(led->gpio_num, led->state);

    return err;
}

uint8_t get_timer_run_state()
{
    return timer_run_state;
}

esp_err_t led_blink_set_period_ms(led_blink_t *led,
                                  uint32_t period_ms)
{
    if (period_ms == led->last_intvl_ms)
    {
        return ESP_OK;
    }

    if (period_ms < 50)
        period_ms = 50;

    if (period_ms > 2000)
        period_ms = 2000;

    led->last_intvl_ms = period_ms;

    uint32_t period_us = (period_ms * 1000UL) / 2;

    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = period_us,
        .flags.auto_reload_on_alarm = 1,
    };

    return gptimer_set_alarm_action(
        led->timer,
        &alarm_config);
}