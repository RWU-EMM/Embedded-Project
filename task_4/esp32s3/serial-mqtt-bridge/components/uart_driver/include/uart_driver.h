#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <stdint.h>
#include "esp_err.h"
#include "driver/uart.h"

#define UART_TX_MAX_SIZE 256

#define RX_BUFF_SIZE 1024
#define TX_BUFF_SIZE 1024

#define QUEUE_LENGTH 20

/**
 * @brief enum for baudrate
 * @note update get_baudrate() for new values
 */
typedef enum
{
    _9600,
    _115200,
} uart_baudrate_e;

typedef struct uart_driver_config
{
    int tx_pin;
    int rx_pin;
    uart_baudrate_e baudrate;
    uart_port_t uart_port;
    uart_word_length_t data_bits;
    uart_stop_bits_t stop_bits;
    uart_parity_t parity;

} uart_driver_config_t;

typedef struct uart_driver_handle uart_driver_handle_t;
typedef void (*uart_rx_cb_t)(uint8_t *data, uint16_t len);

typedef struct uart_driver_handle
{
    uart_driver_config_t config;
    QueueHandle_t rx_queue;
    QueueHandle_t tx_queue;
    TaskHandle_t rx_task_handle;
    TaskHandle_t tx_task_handle;
    uart_rx_cb_t rx_cb;
    uint8_t rx_buffer[RX_BUFF_SIZE + 1];
} uart_driver_handle_t;

esp_err_t init_uart_driver(uart_driver_handle_t *handle);
esp_err_t uart_send(uart_driver_handle_t *handle, const uint8_t *data, uint16_t len);
esp_err_t deinit_uart_driver(uart_driver_handle_t *handle);

#endif // UART_DRIVER_H