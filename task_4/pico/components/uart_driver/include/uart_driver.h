#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

#include "hardware/uart.h"

#define UART_RX_BUFFER_SIZE 1024
#define UART_TX_DMA_MAX_SIZE 1024

typedef void (*uart_rx_event_callback_t)(void);

typedef struct
{
    uart_inst_t *uart;

    uint8_t tx_pin;
    uint8_t rx_pin;

    uint32_t baudrate;

    uint8_t data_bits;
    uint8_t stop_bits;

    uart_parity_t parity;

    bool hw_flow;

    uart_rx_event_callback_t rx_event_callback;

} uart_config_t;

typedef struct
{
    uart_config_t config;

    volatile uint8_t rx_buffer[UART_RX_BUFFER_SIZE];

    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;

    int dma_tx_channel;

    volatile bool tx_dma_busy;

} uart_handle_t;

/* API */

bool uart_driver_init( uart_handle_t *handle, uart_config_t *config);

void uart_driver_deinit( uart_handle_t *handle);

bool uart_driver_write_dma( uart_handle_t *handle, const uint8_t *data, uint16_t length);

void uart_driver_write_blocking( uart_handle_t *handle, const uint8_t *data,
    uint16_t length);

bool uart_driver_read_byte(uart_handle_t *handle, uint8_t *byte);

uint16_t uart_driver_available( uart_handle_t *handle);

#endif // UART_DRIVER_H