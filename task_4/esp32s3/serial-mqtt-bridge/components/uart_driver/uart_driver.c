
#include "uart_driver.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_system.h"
#include "esp_log.h"

#include "driver/uart.h"
#include "driver/gpio.h"

typedef struct
{
    uint8_t data[UART_TX_MAX_SIZE];
    uint16_t len;
} uart_tx_msg_t;

static int get_baudrate(uart_baudrate_e baudrate)
{
    switch (baudrate)
    {
    case _9600:
    {
        return 9600;
    }
    case _115200:
    {
        return 115200;
    }
    default:
    {
        return 115200;
    }
    }
}

static void uart_tx_task(void *params)
{
    uart_driver_handle_t *handle = (uart_driver_handle_t *)params;
    uart_tx_msg_t msg;
    while (1)
    {
        if (xQueueReceive(handle->tx_queue, &msg, portMAX_DELAY))
        {
            int len = uart_write_bytes(handle->config.uart_port, (const char *)msg.data, msg.len);
            if (len != msg.len)
            {
                ESP_LOGI("TX", "[%d]: Write failed", handle->config.uart_port);
            }
        }
    }
}

static void uart_rx_event_task(void *params)
{
    uart_driver_handle_t *handle = (uart_driver_handle_t *)params;

    uart_event_t event;
    while (1)
    {
        if (xQueueReceive(handle->rx_queue, (void *)&event, (TickType_t)portMAX_DELAY))
        {
            switch (event.type)
            {
            // Event of UART receiving data
            /*We'd better handler data event fast, there would be much more data events than
            other types of events. If we take too much time on data event, the queue might
            be full.*/
            case UART_DATA:
            {
                int len = uart_read_bytes(handle->config.uart_port,
                                          handle->rx_buffer, event.size,
                                          portMAX_DELAY);
                if (len > 0)
                {
                    if (len >= RX_BUFF_SIZE)
                    {
                        len = RX_BUFF_SIZE - 1;
                    }

                    handle->rx_buffer[len] = '\0';

                    if (handle->rx_cb)
                    {
                        handle->rx_cb(handle->rx_buffer, len);
                    }
                    else
                    {
                        ESP_LOGI("RX", "[%d]: %.*s", handle->config.uart_port, len, handle->rx_buffer);
                    }
                }
                break;
            }
            case UART_FIFO_OVF:
            {
                ESP_LOGW(__func__, "HW FIFO Overflow");

                uart_flush_input(handle->config.uart_port);
                xQueueReset(handle->rx_queue);

                break;
            }

            default:
            {
                ESP_LOGW(__func__, "[%d]: Unhandled event type", handle->config.uart_port);
            }
            }
        }
    }
}

/**
 * @brief Init UART Driver
 * @param uart_driver_config
 * @return ESP_OK on sucess, else esp_err_t
 */
esp_err_t init_uart_driver(uart_driver_handle_t *handle)
{
    esp_err_t ret = ESP_OK;
    if (!handle)
    {
        ESP_LOGE(__func__, "NULL Handle");
        return ESP_ERR_INVALID_ARG;
    }

    uart_config_t uart_config = {
        .baud_rate = get_baudrate(handle->config.baudrate),
        .data_bits = handle->config.data_bits,
        .parity = handle->config.parity,
        .stop_bits = handle->config.stop_bits,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ret = uart_driver_install(handle->config.uart_port,
                              RX_BUFF_SIZE * 2,
                              TX_BUFF_SIZE * 2,
                              (int)QUEUE_LENGTH,
                              &handle->rx_queue,
                              0);
    if (ESP_OK != ret)
    {
        goto cleanup;
    }

    ESP_ERROR_CHECK(uart_param_config(handle->config.uart_port, &uart_config));

    //@todo: do something about rx tx gpio
    // @note: Set UART pins (using UART0 default pins ie no changes.)
    uart_set_pin(handle->config.uart_port,
                 UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE);

    // Create a task to handler UART event from ISR
    xTaskCreate(uart_rx_event_task, "uart_rx_event_task",
                4096, handle,
                configMAX_PRIORITIES - 1, &handle->rx_task_handle);

    handle->tx_queue = xQueueCreate(QUEUE_LENGTH, sizeof(uart_tx_msg_t));

    if (!handle->tx_queue)
    {
        return ESP_ERR_NO_MEM;
    }

    xTaskCreate(uart_tx_task, "uart_tx_task",
                4096, handle,
                configMAX_PRIORITIES - 2, &handle->tx_task_handle);

    return ESP_OK;
cleanup:
    return ret;
}

esp_err_t uart_send(uart_driver_handle_t *handle, const uint8_t *data, uint16_t len)
{
    if (!handle || !data || !len)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (len > UART_TX_MAX_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }
    uart_tx_msg_t msg = {0};
    memcpy(msg.data, data, len);
    msg.len = len;
    if (xQueueSend(handle->tx_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t deinit_uart_driver(uart_driver_handle_t *handle)
{
    if (!handle)
    {
        return ESP_ERR_INVALID_ARG;
    }

    vTaskDelete(handle->rx_task_handle);
    vTaskDelete(handle->tx_task_handle);

    vQueueDelete(handle->tx_queue);

    uart_driver_delete(handle->config.uart_port);

    memset(handle, 0, sizeof(*handle));

    return ESP_OK;
}