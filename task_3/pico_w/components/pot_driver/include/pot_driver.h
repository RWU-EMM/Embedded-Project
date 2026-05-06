#ifndef POT_DRIVER_H
#define POT_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

// Initialize potentiometer (GPIO26 → ADC0)
void init_pot(uint8_t pot_pin);

// Read raw ADC value (0–4095)
uint16_t pot_read_raw(void);

// Read voltage (0–3.3V)
float pot_read_voltage(void);

// Map raw ADC value to custom range
uint32_t pot_map(uint16_t raw,
                 uint16_t in_min,
                 uint16_t in_max,
                 uint32_t out_min,
                 uint32_t out_max);

// High-level helper: mapped value directly
uint32_t pot_read_mapped(uint32_t out_min, uint32_t out_max);

// Optional: smoothed value
uint16_t pot_read_smooth(uint8_t samples);

#endif // POT_DRIVER_H