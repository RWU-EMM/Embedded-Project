#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "driver/gpio.h"

#include "uart_driver.h"
#include "mqtt_manager.h"
#include "mqtt_service.h"

#include "cJSON.h"

#define CLI_HANDLE_UART_COMPORT UART_NUM_1
#define DEVICE_TOKEN "PICOE"

static char g_cmd_topic[64];
static char g_data_topic[64];

static uart_driver_handle_t s_cli_uart;

static uint32_t g_seq = 0;

static void uart_rx_cb(uint8_t *data, uint16_t len)
{
    if (!data || !len)
    {
        return;
    }

    ESP_LOGI("UART<<", "%.*s", len, data);

    char uart_msg[128];

    if (len >= sizeof(uart_msg))
    {
        len = sizeof(uart_msg) - 1;
    }

    memcpy(uart_msg, data, len);

    uart_msg[len] = '\0';

    while (len && (uart_msg[len - 1] == '\n' || uart_msg[len - 1] == '\r'))
    {
        uart_msg[len - 1] = '\0';
        len--;
    }

    cJSON *root = cJSON_CreateObject();

    if (!root)
    {
        return;
    }

    cJSON_AddStringToObject(root, "token", "iem2026");

    cJSON_AddStringToObject(root, "source", "nano");

    cJSON_AddNumberToObject(root, "seq", g_seq++);

    cJSON_AddStringToObject(root, "uart", uart_msg);

    char *json = cJSON_PrintUnformatted(root);

    if (json)
    {
        ESP_LOGI("MQTT>>", "%s", json);

        mqtt_service_publish(g_data_topic, json, 0, 0);

        free(json);
    }

    cJSON_Delete(root);
}

static void mqtt_cmd_cb(const char *topic, const char *data, int data_len)
{
    ESP_LOGI("MQTT<<", "[%s] %.*s", topic, data_len, data);

    uart_send(&s_cli_uart, (const uint8_t *)data, data_len);
}

void app_main(void)
{
    ESP_LOGI(__func__, "SYS INIT ...");

    snprintf(g_cmd_topic, sizeof(g_cmd_topic), "/cmd/%s", DEVICE_TOKEN);

    snprintf(g_data_topic, sizeof(g_data_topic), "/ack/%s", DEVICE_TOKEN);

    s_cli_uart.config.baudrate = _115200;
    s_cli_uart.config.uart_port = CLI_HANDLE_UART_COMPORT;
    s_cli_uart.config.data_bits = UART_DATA_8_BITS;
    s_cli_uart.config.stop_bits = UART_STOP_BITS_1;
    s_cli_uart.config.parity = UART_PARITY_DISABLE;

#if (CLI_HANDLE_UART_COMPORT == UART_NUM_1)
    s_cli_uart.config.tx_pin = GPIO_NUM_17;
    s_cli_uart.config.rx_pin = GPIO_NUM_18;
#endif

    s_cli_uart.rx_cb = uart_rx_cb;

    ESP_ERROR_CHECK(init_uart_driver(&s_cli_uart));

    init_mqtt_client();

    ESP_ERROR_CHECK(mqtt_service_register_handler(g_cmd_topic, mqtt_cmd_cb));

    ESP_ERROR_CHECK(mqtt_service_start());

    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_ERROR_CHECK(mqtt_service_subscribe(g_cmd_topic, 1));

    ESP_LOGI(__func__, "UART <-> MQTT bridge ready");

    while (1)
    {
        // ESP_LOGI("MQTT", "connected=%d state=%d",
        //          mqtt_service_is_connected(),
        //          mqtt_service_get_state());

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}