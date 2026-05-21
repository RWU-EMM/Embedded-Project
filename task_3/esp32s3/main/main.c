#include <stdio.h>

#include "main.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_timer.h"

#define MQTT_SUB_TIMEOUT_MS 10000

#define ACK_TIMEOUT (10ULL * 1000000ULL)

void pot_cb(uint32_t value);

led_blink_t hearbeat_led = {
    .gpio_num = _HEARTBEAT_LED_PIN,
};

led_blink_t fault_led = {
    .gpio_num = _FAULT_LED_PIN,
};

led_blink_t mirror_led = {
    .gpio_num = _MIRROR_LED_PIN,
};

potentiometer_t pot_1 = {
    .unit = POT_1_ADC_UNIT,
    .channel = POT_1_ADC_CHANNEL,
};

static void device_monitor_task(void *arg)
{
    while (1)
    {
        mqtt_state_t mqtt_state = mqtt_service_get_state();
        if (MQTT_STATE_CONNECTED != mqtt_state)
        {
            // communication lost

            led_blink_start(&fault_led);
            led_blink_set_period_ms(&fault_led, 250);

            mirror_led.ctrl_en = 0;
            ESP_LOGI(__func__, "MQTT Broker Not Connected");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        device_entry_t *dev = device_find_by_token("PICO");

        if (dev && dev->authenticated)
        {
            uint64_t now = esp_timer_get_time();

            /*
             * ACK TIMEOUT
             */

            if ((now - dev->last_ack_time_us) > ACK_TIMEOUT)
            {
                /*
                 * fault condition
                 */

                led_blink_start(&fault_led);

                led_blink_set_period_ms(
                    &fault_led,
                    2000);

                mirror_led.ctrl_en = 0;
                ESP_LOGI(__func__, "Fault");
            }
            else
            {
                /*
                 * healthy
                 */

                fault_led.ctrl_en = 0;

                led_blink_start(&mirror_led);

                dev->mirror_blink_ms = (dev->mirror_blink_ms <= 50) ? 50 : dev->mirror_blink_ms;
                dev->mirror_blink_ms = (dev->mirror_blink_ms >= 2000) ? 2000 : dev->mirror_blink_ms;

                led_blink_set_period_ms(&mirror_led, dev->mirror_blink_ms);
                ESP_LOGI(__func__, "Mirror LED Intvl: %lu", dev->mirror_blink_ms);
            }

            // Pico for STATUS
            device_send_command("PICO:CMD:STATUS");
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_led_blink(&hearbeat_led, GPIO_PULLUP_ONLY));

    ESP_ERROR_CHECK(led_blink_start(&hearbeat_led));

    ESP_ERROR_CHECK(init_led_blink(&fault_led, GPIO_PULLUP_ONLY));

    // ESP_ERROR_CHECK(led_blink_start(&fault_led));

    // ESP_ERROR_CHECK(led_blink_set_frequency(&fault_led, 1.0f));

    ESP_ERROR_CHECK(init_led_blink(&mirror_led, GPIO_PULLUP_ONLY));

    // ESP_ERROR_CHECK(led_blink_start(&mirror_led));

    // ESP_ERROR_CHECK(led_blink_set_frequency(&mirror_led, 1.0f));

    // pot_1.cb = pot_cb;
    // ESP_ERROR_CHECK(init_potentiometer(&pot_1));
    // ESP_ERROR_CHECK(potentiometer_start(&pot_1));

    /* 1. Start MQTT (includes WiFi + service init) */
    init_mqtt_client();

    /* 3. Start UART command parsing */
    init_uart_cmd_parsing();

    uint32_t waited = 0;

    while (!mqtt_service_all_subscribed())
    {
        vTaskDelay(pdMS_TO_TICKS(250));
        waited += 250;
        if (waited >= MQTT_SUB_TIMEOUT_MS)
        {
            ESP_LOGE("APP", "MQTT subscribe timeout");
            ESP_LOGE("APP", "RESTARTING....");
            esp_restart();
            return;
        }
    }

    device_connect("PICO", 1);
    vTaskDelay(pdMS_TO_TICKS(5000));
    device_send_command("PICO:LED:EN=1:BLINK=500:POT=0");
    vTaskDelay(pdMS_TO_TICKS(50));
    // device_send_command("PICO:POT=0");
    // vTaskDelay(pdMS_TO_TICKS(50));

    xTaskCreate(device_monitor_task, "dev_monitor", 4096, NULL, 5, NULL);

    ESP_LOGI(__func__, "System ready");

    /* 4. Optional: keep main alive (or do nothing) */
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void pot_cb(uint32_t value)
{
    ESP_LOGI(__func__, "Pot value: %lu", value);
}