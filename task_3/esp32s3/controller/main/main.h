#ifndef MAIN_H
#define MAIN_H

#include "led_blink_handler.h"
#include "potentiometer_driver.h"
#include "uart_cmd_parser.h"
#include "mqtt_manager.h"

typedef enum {
    _HEARTBEAT_LED_PIN = GPIO_NUM_4,
    _FAULT_LED_PIN = GPIO_NUM_5,
    _USER_LED_PIN = GPIO_NUM_5,
    POT_1_ADC_UNIT = ADC_UNIT_1,
    POT_1_ADC_CHANNEL = ADC_CHANNEL_5, // gpio 6

}macros_e;

extern led_blink_t hearbeat_led;
extern led_blink_t fault_led;
extern potentiometer_t pot_1;

#endif // MAIN_H