#include "pot_driver.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/adc.h"


#define POT_GPIO 26
#define POT_ADC_CHANNEL 0

#define ADC_MAX 4095.0f
#define ADC_VREF 3.3f


// Initialize potentiometer (GPIO26 → ADC0)
void init_pot(uint8_t pot_pin)
{
    adc_init();

    // GPIO26 → ADC0
    adc_gpio_init(pot_pin);
    uint8_t adc_channel = pot_pin - 26; // GPIO26 → ADC0
    adc_select_input(adc_channel);
}

// Returns raw ADC value (0–4095)
uint16_t pot_read_raw(void)
{
    return adc_read(); // 12-bit: 0–4095
}

// Converts raw ADC value to voltage (0–3.3V)
float pot_read_voltage(void)
{
    uint16_t raw = pot_read_raw();
    return (raw * ADC_VREF) / ADC_MAX;
}

// Maps raw ADC value to custom range (e.g., 0–100%)
uint32_t pot_map(uint16_t raw,
                 uint16_t in_min,
                 uint16_t in_max,
                 uint32_t out_min,
                 uint32_t out_max)
{
    if (in_max == in_min)
        return out_min; // avoid div-by-zero

    return (uint32_t)(
        (raw - in_min) * (out_max - out_min) /
        (in_max - in_min) + out_min);
}


// Reads raw ADC and maps it to specified range in one step
uint32_t pot_read_mapped(uint32_t out_min, uint32_t out_max)
{
    uint16_t raw = pot_read_raw();
    return pot_map(raw, 0, 4095, out_min, out_max);
}

// Reads multiple samples and returns their average to reduce noise
uint16_t pot_read_smooth(uint8_t samples)
{
    if (samples == 0)
        samples = 1;

    uint32_t sum = 0;

    for (uint8_t i = 0; i < samples; i++)
    {
        sum += pot_read_raw();
    }

    return (uint16_t)(sum / samples);
}