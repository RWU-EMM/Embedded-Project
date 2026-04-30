#include <stdio.h>

#include "main.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void pot_cb(uint32_t value);

led_blink_t hearbeat_led = {
    .gpio_num = _HEARTBEAT_LED_PIN,
};

led_blink_t fault_led = {
    .gpio_num = _FAULT_LED_PIN,
};

potentiometer_t pot_1 = {
    .unit = POT_1_ADC_UNIT,
    .channel = POT_1_ADC_CHANNEL,
};

void app_main(void)
{
    ESP_ERROR_CHECK(init_led_blink(&hearbeat_led,GPIO_PULLUP_ONLY));

    ESP_ERROR_CHECK(led_blink_start(&hearbeat_led));

    ESP_ERROR_CHECK(init_led_blink(&fault_led,GPIO_PULLUP_ONLY));

    ESP_ERROR_CHECK(led_blink_start(&fault_led));


    ESP_ERROR_CHECK(led_blink_set_frequency(&fault_led,3.0f));

    // pot_1.cb = pot_cb;
    // ESP_ERROR_CHECK(init_potentiometer(&pot_1));
    // ESP_ERROR_CHECK(potentiometer_start(&pot_1));

    init_uart_cmd_parsing();

    init_mqtt_client();
    
    // while (1) {
    //     ESP_LOGI("MAIN", "Pot value: %lu", potentiometer_get_value(&pot_1));
    //     vTaskDelay(pdMS_TO_TICKS(500));
    // }
}

void pot_cb(uint32_t value)
{
   ESP_LOGI(__func__, "Pot value: %lu", value);
}