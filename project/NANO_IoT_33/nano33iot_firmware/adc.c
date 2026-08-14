#include "adc.h"
#include "config.h"
#include <Arduino.h>

void adc_init(void)
{
    analogReadResolution(ADC_RESOLUTION_BITS);
    pinMode(ADC_INPUT_PIN, INPUT);
}

uint16_t adc_read(void)
{
    return (uint16_t)analogRead(ADC_INPUT_PIN);
}
