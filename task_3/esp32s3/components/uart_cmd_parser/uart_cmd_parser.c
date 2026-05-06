#include "uart_cmd_parser.h"
#include "led_blink_handler.h"
#include "device_handler.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#define UART_PORT UART_NUM_0
#define BUF_SIZE 128
#define CMD_BUFFER_SIZE 64

static char cmd_buffer[CMD_BUFFER_SIZE];
static uint8_t cmd_index = 0;

void process_command(const char *cmd)
{
    //@Note: cmd parsing taken care in device_send_cmd
    //@todo: based on mqtt ack send feedback to uart

    if (strncmp(cmd, "CONN=", 5) == 0)
    {
        char buf[64];
        strncpy(buf, cmd + 5, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        char *saveptr;
        char *token = strtok_r(buf, ":", &saveptr);

        if (!token)
        {
            ESP_LOGW("UART", "Invalid CONN format");
            return;
        }

        uint8_t bypass = 0; // default

        char *pair;
        while ((pair = strtok_r(NULL, ":", &saveptr)))
        {
            char *eq = strchr(pair, '=');
            if (!eq)
                continue;

            *eq = '\0';

            char *key = pair;
            char *val = eq + 1;

            if (strcmp(key, "BYPASS") == 0)
            {
                bypass = atoi(val);
            }
        }

        device_connect(token, bypass);
    }
    else
    {
        device_send_command(cmd);
    }
}

static void uart_parse_cmd_task(void *arg)
{
    uint8_t data[BUF_SIZE];

    while (1)
    {
        int len = uart_read_bytes(UART_PORT, data, BUF_SIZE, pdMS_TO_TICKS(100));

        if (len > 0)
        {
            for (int i = 0; i < len; i++)
            {
                char c = data[i];

                if (c == '\n' || c == '\r')
                {
                    if (cmd_index > 0)
                    {
                        cmd_buffer[cmd_index] = '\0';

                        process_command(cmd_buffer);

                        cmd_index = 0;
                    }
                }
                else
                {
                    if (cmd_index < CMD_BUFFER_SIZE - 1)
                    {
                        cmd_buffer[cmd_index++] = toupper((unsigned char)c);
                    }
                }
            }
        }
    }
}

void init_uart_cmd_parsing(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };

    uart_driver_install(UART_PORT, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_PORT, &uart_config);

    xTaskCreate(uart_parse_cmd_task, "uart_cmd_task", 4096, NULL, 5, NULL);
}
