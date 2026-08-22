#include "dac.h"
#include "config.h"
#include <Arduino.h>

void dac_init(void)
{
    analogWriteResolution(DAC_RESOLUTION_BITS);
    analogWrite(DAC_OUTPUT_PIN, 0);
}

void dac_write(uint16_t value)
{
    const uint16_t max_value = (uint16_t)((1UL << DAC_RESOLUTION_BITS) - 1UL);

    if (value > max_value) {
        value = max_value;
    }

    analogWrite(DAC_OUTPUT_PIN, (int)value);
}
