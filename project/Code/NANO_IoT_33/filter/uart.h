#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UART_PARITY_NONE = 0,
    UART_PARITY_EVEN = 1,
    UART_PARITY_ODD = 2
} uart_parity_t;

void uart_init(void);
bool uart_available(void);
int uart_read_byte(void);
bool uart_write_byte(uint8_t data);

#ifdef __cplusplus
}
#endif

#endif
