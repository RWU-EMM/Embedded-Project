#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "wifi_driver.h"
#include "led_handler.h"
#include "rgb_led.h"
#include "gpio_driver.h"
#include "button_handler.h"
#include "dht11_driver.h"
#include "servo_driver.h"
#include "pot_driver.h"
#include "uart_driver.h"
#include "serial_data_parser.h"
#include "json_parser.h"

#define SERVO_GPIO 7

#define BUTTON_1_GPIO 10
#define BUTTON_2_GPIO 11
#define BUTTON_3_GPIO 14
#define BUTTON_4_GPIO 15

#define DHT11_GPIO 0

#define DHT11_DATA_SEND_PERIOD 2000000

led_t heart_beat_led = {
    .pin_num = CYW43_WL_GPIO_LED_PIN,
    .en_ctrl = 1,
    .hw_type = _LED_HW_CYW43,
};

rgb_led_t rgb = {
    .pin = 1,
    .led_count = 4,
    .order = RGB_ORDER_GRB,
};

typedef struct
{
    uint8_t top : 1;
    uint8_t right : 1;
    uint8_t bottom : 1;
    uint8_t left : 1;
    uint8_t reserved : 4;
} button_t;

typedef union
{
    button_t button;
    uint8_t raw_button;
} button_u;

static button_u button;

static uart_handle_t cli_uart;
static serial_data_parser_t parser;
static json_packet_t json_packet = {0};
static dht11_t dht;
static int servo1 = -1;
static float current_servo_angle = 0.0f;
static uint8_t g_led_color_idx[4] = {0};
static const char *g_colors[] = {"OFF", "RED", "GREEN", "BLUE", "YELLOW", "CYAN", "PURPLE", "WHITE"};
static absolute_time_t last_dht_tx;

static void init_uart(void);
static void process_uart_data(void);
static void uart_send_line(const char *str);
static void rgb_apply_color(uint8_t led, uint8_t idx);
static void button_callback(uint gpio, button_event_t event, void *user_data);
static void init_buttons(void);
static void init_dht11(void);
void button_press_handler();
void servo_update_handler();
void execute_json_packet();
static void send_temp_hum(void);

int main(void)
{
    stdio_init_all();
    sleep_ms(500);
    printf("Initiasling System\n");

    if (cyw43_arch_init())
    {
        printf("Cyw43 Init Failed");
        return -1;
    }

    init_led(&heart_beat_led);
    led_start_blink(&heart_beat_led, 1000);

    init_rgb_led(&rgb);
    // rgb_led_on_pixel(&rgb, 0, 255, 0, 0);
    // rgb_led_on_pixel(&rgb, 1, 0, 255, 0);
    rgb_led_on_pixel(&rgb, 2, 0, 0, 255);

    servo_driver_init();
    servo1 = servo_attach(SERVO_GPIO);

    init_uart();

    init_buttons();

    // init_dht11();

    sleep_ms(500);
    printf("System Ready\n");

    while (1)
    {

        process_uart_data();

        button_press_handler();

        servo_update_handler();

        rgb_led_update(&rgb);

        if (absolute_time_diff_us(last_dht_tx, get_absolute_time()) >= DHT11_DATA_SEND_PERIOD)
        {
            last_dht_tx = get_absolute_time();

            send_temp_hum();
        }

        sleep_ms(100);

        tight_loop_contents();
    }
}

static void init_dht11(void)
{
    dht11_init(&dht, DHT11_GPIO);
    dht11_start_periodic_read(&dht, 2000);
    printf("DHT11 Initialized\n");
}

static void init_uart(void)
{
    uart_config_t uart_cfg = {
        .uart = uart1,
        .tx_pin = 4,
        .rx_pin = 5,
        .baudrate = 115200,
        .data_bits = 8,
        .stop_bits = 1,
        .parity = UART_PARITY_NONE,
        .hw_flow = false,
        .rx_event_callback = NULL,
    };

    uart_driver_init(&cli_uart, &uart_cfg);

    init_serial_data_parser(&parser);

    const char *msg = "UART DMA Driver Ready\r\n";

    uart_driver_write_dma(&cli_uart, (const uint8_t *)msg, strlen(msg));

    printf("UART Initialized\n");
}

static void uart_send_line(const char *str)
{
    if (!str)
    {
        return;
    }

    while (!uart_driver_write_dma(&cli_uart, (const uint8_t *)str, strlen(str)))
    {
        tight_loop_contents();
    }

    while (!uart_driver_write_dma(&cli_uart, (const uint8_t *)"\n", 1))
    {
        tight_loop_contents();
    }
}

static void process_uart_data(void)
{
    uint8_t byte;

    while (uart_driver_read_byte(&cli_uart, &byte))
    {
        if (serial_data_parser_feed(&parser, byte))
        {
            printf("RX: %s\n", parser.packet.data);

            if (json_parse((const char *)parser.packet.data, parser.packet.len, &json_packet))
            {
                printf("Valid JSON\n");
                execute_json_packet();
            }
            serial_data_parser_reset(&parser);
        }
    }
}

static void rgb_apply_color(uint8_t led, uint8_t idx)
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    switch (idx)
    {
    case 1:
        r = 255;
        break;

    case 2:
        g = 255;
        break;

    case 3:
        b = 255;
        break;

    case 4:
        r = 255;
        g = 255;
        break;

    case 5:
        g = 255;
        b = 255;
        break;

    case 6:
        r = 255;
        b = 255;
        break;

    case 7:
        r = 255;
        g = 255;
        b = 255;
        break;
    }

    rgb_led_on_pixel(&rgb, led, r, g, b);
}

static void button_callback(uint gpio, button_event_t event, void *user_data)
{
    (void)user_data;

    if (event != BUTTON_EVENT_SHORT_PRESS)
    {
        return;
    }

    switch (gpio)
    {
    case BUTTON_1_GPIO:
    {
        button.button.top = 1;
        break;
    }
    case BUTTON_2_GPIO:
    {
        button.button.right = 1;
        break;
    }
    case BUTTON_3_GPIO:
    {
        button.button.bottom = 1;
        break;
    }
    case BUTTON_4_GPIO:
    {
        button.button.left = 1;
        break;
    }

    default:
        button.raw_button = 0;
        break;
    }
}

static void init_buttons(void)
{
    const uint button_gpios[] = {
        BUTTON_1_GPIO,
        BUTTON_2_GPIO,
        BUTTON_3_GPIO,
        BUTTON_4_GPIO};

    for (uint i = 0; i < 4; i++)
    {
        button_config_t btn_cfg = {
            .gpio = button_gpios[i],
            .active_low = true,
            .debounce_ms = 50,
            .long_press_ms = 1000,
            .callback = button_callback,
            .user_data = NULL};

        button_handler_init(&btn_cfg);
    }

    printf("Buttons Initialized\n");
}

int get_button_index(uint8_t raw_button)
{
    switch (raw_button)
    {
    case 1:
        return 0; // top
    case 2:
        return 1; // right
    case 4:
        return 2; // bottom
    case 8:
        return 3; // left
    default:
        return -1; // invalid or multiple buttons
    }
}

void button_press_handler()
{
    if (button.raw_button)
    {
        uint8_t led_idx = get_button_index(button.raw_button);
        uint8_t color_idx = ++g_led_color_idx[led_idx];

        color_idx %= 8;

        g_led_color_idx[led_idx] = color_idx;
        rgb_apply_color(led_idx, color_idx);

        char resp[64];
        snprintf(resp, sizeof(resp), "ACK:LED:%d:%s", led_idx, g_colors[color_idx]);
        uart_send_line(resp);

        button.raw_button = 0;
    }
}

void servo_update_handler()
{
    static float prvs_servo_angle = 0;
    if (abs(prvs_servo_angle - current_servo_angle) > 5)
    {
        servo_write_angle(servo1, current_servo_angle);

        prvs_servo_angle = current_servo_angle;

        char resp[64];
        snprintf(resp, sizeof(resp), "ACK:SERVO:%d", current_servo_angle);
        uart_send_line(resp);
    }
}

void execute_json_packet()
{
    char *cmd = json_packet.cmd;

    if (!cmd[0])
    {
        return;
    }

    // LED:N:NEXT
    if (strncmp(cmd, "LED:", 4) == 0)
    {
        int led = 0;

        if (sscanf(cmd, "LED:%d:NEXT", &led) == 1)
        {
            if (led >= 1 && led <= 4)
            {
                switch (led)
                {
                case 1:
                {
                    button.button.top = 1;
                    break;
                }
                case 2:
                {
                    button.button.right = 1;
                    break;
                }
                case 3:
                {
                    button.button.bottom = 1;
                    break;
                }
                case 4:
                {
                    button.button.left = 1;
                    break;
                }

                default:
                    button.raw_button = 0;
                    break;
                }
            }
        }
    }

    // SERVO:angle
    else if (strncmp(cmd, "SERVO:", 7) == 0)
    {
        int angle = 0;

        if (sscanf(cmd, "SERVO:%d", &angle) == 1)
        {
            if (angle < 0)
            {
                angle = 0;
            }

            if (angle > 180)
            {
                angle = 180;
            }

            current_servo_angle = angle;
        }
    }

    // STATUS
    else if (strcmp(cmd, "STATUS") == 0)
    {
        char resp[256];

        snprintf(resp, sizeof(resp), "STATUS:LED1=%s,LED2=%s,LED3=%s,LED4=%s,SERVO=%d,TEMP=%.1f,HUM=%d",
                 g_colors[g_led_color_idx[0]],
                 g_colors[g_led_color_idx[1]],
                 g_colors[g_led_color_idx[2]],
                 g_colors[g_led_color_idx[3]],
                 current_servo_angle,
                 dht.temperature,
                 (int)dht.humidity);

        uart_send_line(resp);
    }

    memset(&json_packet, 0, sizeof(json_packet));
}


static void send_temp_hum(void)
{
    char resp[64];

    snprintf(resp, sizeof(resp), "TEMP:%.1f", dht.temperature);

    uart_send_line(resp);

    snprintf(resp, sizeof(resp), "HUM:%d", (int)dht.humidity);

    uart_send_line(resp);
}