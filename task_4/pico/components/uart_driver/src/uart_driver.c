#include "uart_driver.h"
#include "pico/stdlib.h"

#include "hardware/irq.h"
#include "hardware/dma.h"

#include <string.h>

static uart_handle_t *uart0_handle_ptr = NULL;
static uart_handle_t *uart1_handle_ptr = NULL;

static inline void rx_buffer_put(uart_handle_t *handle, uint8_t data)
{
    uint16_t next = (handle->rx_head + 1) % UART_RX_BUFFER_SIZE;

    if (next != handle->rx_tail)
    {
        handle->rx_buffer[handle->rx_head] = data;

        handle->rx_head = next;
    }
}

static inline bool rx_buffer_get(uart_handle_t *handle, uint8_t *data)
{
    if (handle->rx_head == handle->rx_tail)
    {
        return false;
    }

    *data = handle->rx_buffer[handle->rx_tail];

    handle->rx_tail = (handle->rx_tail + 1) % UART_RX_BUFFER_SIZE;

    return true;
}

static void uart_irq_common_handler(uart_handle_t *handle)
{
    while (uart_is_readable(handle->config.uart))
    {
        uint8_t byte = uart_getc(handle->config.uart);

        rx_buffer_put(handle, byte);
    }

    if (handle->config.rx_event_callback)
    {
        handle->config.rx_event_callback();
    }
}

static void uart0_irq_handler()
{
    if (uart0_handle_ptr)
    {
        uart_irq_common_handler(uart0_handle_ptr);
    }
}

static void uart1_irq_handler()
{
    if (uart1_handle_ptr)
    {
        uart_irq_common_handler(uart1_handle_ptr);
    }
}

static void dma_handler()
{
    if (uart0_handle_ptr)
    {
        if (dma_channel_get_irq0_status(uart0_handle_ptr->dma_tx_channel))
        {
            dma_channel_acknowledge_irq0(uart0_handle_ptr->dma_tx_channel);

            uart0_handle_ptr->tx_dma_busy = false;
        }
    }

    if (uart1_handle_ptr)
    {
        if (dma_channel_get_irq0_status(uart1_handle_ptr->dma_tx_channel))
        {
            dma_channel_acknowledge_irq0(uart1_handle_ptr->dma_tx_channel);

            uart1_handle_ptr->tx_dma_busy = false;
        }
    }
}

bool uart_driver_init(uart_handle_t *handle, uart_config_t *config)
{
    if (!handle || !config)
    {
        return false;
    }

    memset(handle, 0, sizeof(uart_handle_t));

    handle->config = *config;

    uart_init(config->uart, config->baudrate);

    gpio_set_function(config->tx_pin, UART_FUNCSEL_NUM(config->uart, config->tx_pin));
    gpio_set_function(config->rx_pin, UART_FUNCSEL_NUM(config->uart, config->rx_pin));

    uart_set_hw_flow(config->uart, false, false);

    uart_set_format(config->uart, config->data_bits, config->stop_bits, config->parity);

    uart_set_fifo_enabled(config->uart, true);

    handle->dma_tx_channel = dma_claim_unused_channel(true);

    dma_channel_config dma_cfg = dma_channel_get_default_config(handle->dma_tx_channel);

    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_8);

    channel_config_set_read_increment(&dma_cfg, true);

    channel_config_set_write_increment(&dma_cfg, false);

    channel_config_set_dreq(&dma_cfg, uart_get_dreq(config->uart, true));

    dma_channel_configure(handle->dma_tx_channel, &dma_cfg,
                          &uart_get_hw(config->uart)->dr,
                          NULL, 0, false);

    dma_channel_set_irq0_enabled(handle->dma_tx_channel, true);

    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);

    irq_set_enabled(DMA_IRQ_0, true);

    if (config->uart == uart0)
    {
        uart0_handle_ptr = handle;

        irq_set_exclusive_handler(UART0_IRQ, uart0_irq_handler);

        irq_set_enabled(UART0_IRQ, true);
    }
    else
    {
        uart1_handle_ptr = handle;

        irq_set_exclusive_handler(UART1_IRQ, uart1_irq_handler);

        irq_set_enabled(UART1_IRQ, true);
    }

    uart_set_irq_enables(config->uart, true, false);

    return true;
}

void uart_driver_deinit(uart_handle_t *handle)
{
    if (!handle)
    {
        return;
    }

    uart_deinit(handle->config.uart);

    dma_channel_unclaim(handle->dma_tx_channel);
}

bool uart_driver_write_dma(uart_handle_t *handle, const uint8_t *data, uint16_t length)
{
    if (!handle || !data)
    {
        return false;
    }

    if (handle->tx_dma_busy)
    {
        return false;
    }

    handle->tx_dma_busy = true;

    dma_channel_transfer_from_buffer_now(handle->dma_tx_channel, data, length);

    return true;
}

void uart_driver_write_blocking(uart_handle_t *handle, const uint8_t *data, uint16_t length)
{
    if (!handle || !data)
    {
        return;
    }

    uart_write_blocking(handle->config.uart, data, length);
}

bool uart_driver_read_byte(uart_handle_t *handle, uint8_t *byte)
{
    return rx_buffer_get(handle, byte);
}

uint16_t uart_driver_available(uart_handle_t *handle)
{
    if (handle->rx_head >= handle->rx_tail)
    {
        return handle->rx_head - handle->rx_tail;
    }

    return UART_RX_BUFFER_SIZE - handle->rx_tail + handle->rx_head;
}