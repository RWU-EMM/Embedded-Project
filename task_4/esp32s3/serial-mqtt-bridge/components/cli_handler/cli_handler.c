
#include "cli_handler.h"
#include "uart_driver.h"
#include "serial_data_parser.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cJSON.h"

#include "driver/gpio.h"

#include "esp_system.h"
#include "esp_log.h"

#define CLI_HANDLE_UART_COMPORT UART_NUM_0

static uart_driver_handle_t s_cli_uart;

void rx_cb(uint8_t *data, uint16_t len)
{

    if (!data || len == 0)
    {
        ESP_LOGW("RX", "Invalid RX data");
        return;
    }
    ESP_LOGI("RX<<", "%.*s", len, data);
    //
    // Parse serial command into JSON
    //
    cJSON *root = serial_data_parse_to_json((const char *)data, len);

    if (!root)
    {
        ESP_LOGE("RX", "JSON parsing failed");

        return;
    }
    char *json_str = cJSON_PrintUnformatted(root);

    if (!json_str)
    {
        ESP_LOGE("RX", "JSON encode failed");

        cJSON_Delete(root);

        return;
    }
    ESP_LOGI("JSON", "%s", json_str);
    // @todo: implement parsing  here.
    // received data to mqtt
    free(json_str);

    cJSON_Delete(root);
}

esp_err_t init_cli_handler(void)
{
    s_cli_uart.config.baudrate = _115200;
    s_cli_uart.config.uart_port = CLI_HANDLE_UART_COMPORT;
    s_cli_uart.config.data_bits = UART_DATA_8_BITS;
    s_cli_uart.config.stop_bits = UART_STOP_BITS_1;
    s_cli_uart.config.parity = UART_PARITY_DISABLE;

#if (CLI_HANDLE_UART_COMPORT == UART_NUM_1)
    s_cli_uart.config.tx_pin = GPIO_NUM_17;
    s_cli_uart.config.rx_pin = GPIO_NUM_18;
#endif

    s_cli_uart.rx_cb = rx_cb;

    return init_uart_driver(&s_cli_uart);
}
