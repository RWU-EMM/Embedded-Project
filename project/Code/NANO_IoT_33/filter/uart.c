#include "uart.h"
#include "config.h"

/* These functions are implemented in the .ino file because Serial1 is a C++ object. */
extern void uart_port_init(uint32_t baud_rate, uint8_t data_bits,
                           uint8_t stop_bits, uint8_t parity);
extern int uart_port_available(void);
extern int uart_port_read_byte(void);
extern int uart_port_write_byte(uint8_t data);

void uart_init(void)
{
    uart_port_init(UART_BAUD_RATE, UART_DATA_BITS, UART_STOP_BITS, UART_PARITY);
}

bool uart_available(void)
{
    return (uart_port_available() > 0);
}

int uart_read_byte(void)
{
    return uart_port_read_byte();
}

bool uart_write_byte(uint8_t data)
{
    return (uart_port_write_byte(data) == 1);
}


