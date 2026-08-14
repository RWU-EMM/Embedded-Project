#include "config.h"
#include "gpio.h"
#include "uart.h"
#include "adc.h"
#include "dac.h"
#include "timer.h"

/* Arduino's Serial1 is a C++ object. These small C-linkage adapters keep the
 * application modules C-compatible while isolating Arduino C++ from uart.c. */
extern "C" void uart_port_init(uint32_t baud_rate, uint8_t data_bits,
                               uint8_t stop_bits, uint8_t parity)
{
    uint32_t serial_config = SERIAL_8N1;

    if (data_bits == 8U && stop_bits == 1U) {
        if (parity == 1U) {
            serial_config = SERIAL_8E1;
        } else if (parity == 2U) {
            serial_config = SERIAL_8O1;
        }
    }

    Serial1.begin(baud_rate, serial_config);
}

extern "C" int uart_port_available(void)
{
    return Serial1.available();
}

extern "C" int uart_port_read_byte(void)
{
    return Serial1.read();
}

extern "C" int uart_port_write_byte(uint8_t data)
{
    return (Serial1.write(data) == 1U) ? 1 : 0;
}

void setup(void)
{
    gpio_init();
    uart_init();
    adc_init();
    dac_init();
    timer_init();

    gpio_set_rdy(true);
    timer_enable();
}

void loop(void)
{
    while (timer_take_event()) {
        /* Application work belongs here, not in TC3_Handler(). */
        const uint16_t adc_value = adc_read();
        dac_write(adc_value >> (ADC_RESOLUTION_BITS - DAC_RESOLUTION_BITS));

        /* Example non-blocking command input path. */
        while (uart_available()) {
            const int received_byte = uart_read_byte();
            if (received_byte >= 0) {
                (void)uart_write_byte((uint8_t)received_byte);
            }
        }

        gpio_set_rdy(gpio_get_cmd());
    }
}
