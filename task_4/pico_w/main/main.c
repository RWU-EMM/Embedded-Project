#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "cJSON.h"

#include "wifi_driver.h"
#include "mqtt_client.h"

#include "rgb_led.h"
#include "led_handler.h"
#include "pot_driver.h"
#include "uart_driver.h"
#include "serial_parser.h"
#include "serial_data_parser.h"
#include "buzzer_driver.h"
#include "button_handler.h"
#include "gpio_driver.h"
#include "servo_driver.h"

#define MQTT_RETRY_INTERVAL_MS 10000

//
// GPIO DEFINITIONS
//

#define SERVO_GPIO 7

#define BUTTON_1_GPIO 10
#define BUTTON_2_GPIO 11
#define BUTTON_3_GPIO 14
#define BUTTON_4_GPIO 15

#define BUZZER_GPIO 27

#define POT_GPIO 26

#define DHT11_GPIO 0

led_t heart_beat_led = {
    .pin_num = CYW43_WL_GPIO_LED_PIN,
    .en_ctrl = 1,
    .hw_type = _LED_HW_CYW43,
};

rgb_led_t rgb = {
    .pin = 4,
    .led_count = 4,
    .order = RGB_ORDER_GRB,
};
static uint8_t g_led_color_idx[4] = {0};
static const char *g_colors[] = {"OFF", "RED", "GREEN", "BLUE", "YELLOW", "CYAN", "PURPLE", "WHITE"};
static buzzer_t buzzer;
static int servo1 = -1;
static absolute_time_t last_mqtt_retry = {0};
static uart_handle_t cli_uart;
static serial_parser_t parser;
static char g_token[32] = "PICOE";
static char g_cmd_topic[64];
static char g_ack_topic[64];
static char g_data_topic[64];
static float current_servo_angle = 0.0f;
static absolute_time_t last_pot_read;

static void heartbeat_worker_fn(async_context_t *context, async_at_time_worker_t *worker);
static void init_buttons(void);
static void button_callback(uint gpio, button_event_t event, void *user_data);

static async_at_time_worker_t heartbeat_worker = {
    .do_work = heartbeat_worker_fn};

void my_mqtt_cb(const char *topic, const char *data)
{
    if (strcmp(topic, g_ack_topic) != 0 &&
        strcmp(topic, g_data_topic) != 0)
    {
        return;
    }

    printf("MQTT RX [%s]: %s\n", topic, data);

    cJSON *root = cJSON_Parse(data);

    if (!root)
    {
        return;
    }

    cJSON *uart = cJSON_GetObjectItem(root, "uart");

    if (!cJSON_IsString(uart) || !uart->valuestring)
    {
        cJSON_Delete(root);
        return;
    }

    char *msg = uart->valuestring;

    //
    // ACK:LED:n:COLOR
    //

    if (strncmp(msg, "ACK:LED:", 9) == 0)
    {
        int led = 0;
        char color[16] = {0};

        if (sscanf(msg, "ACK:LED:%d:%15s", &led, color) == 2)
        {
            uint8_t idx = 0;

            if (strcmp(color, "RED") == 0)
                idx = 1;
            else if (strcmp(color, "GREEN") == 0)
                idx = 2;
            else if (strcmp(color, "BLUE") == 0)
                idx = 3;
            else if (strcmp(color, "YELLOW") == 0)
                idx = 4;
            else if (strcmp(color, "CYAN") == 0)
                idx = 5;
            else if (strcmp(color, "PURPLE") == 0)
                idx = 6;
            else if (strcmp(color, "WHITE") == 0)
                idx = 7;
            else
                idx = 0;

            if (led >= 1 && led <= 4)
            {
                g_led_color_idx[led - 1] = idx;

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

                rgb_led_on_pixel(&rgb, led - 1, r, g, b);

                printf("LED%d -> %s\n", led, color);
            }
        }
    }

    //
    // ACK:SERVO:angle
    //

    else if (strncmp(msg, "ACK:SERVO:", 11) == 0)
    {
        int angle = 0;

        if (sscanf(msg, "ACK:SERVO:%d", &angle) == 1)
        {
            current_servo_angle = angle;

            printf("SERVO -> %d\n", angle);
        }
    }

    //
    // TEMP:value
    //

    else if (strncmp(msg, "TEMP:", 5) == 0)
    {
        float temp = atof(&msg[5]);

        printf("TEMP -> %.1f C\n", temp);
    }

    //
    // HUM:value
    //

    else if (strncmp(msg, "HUM:", 4) == 0)
    {
        int hum = atoi(&msg[4]);

        printf("HUM -> %d %%\n", hum);
    }

    //
    // BTN:name
    //

    else if (strncmp(msg, "BTN:", 4) == 0)
    {
        printf("BUTTON -> %s\n", &msg[4]);
    }

    //
    // STATUS:...
    //

    else if (strncmp(msg, "STATUS:", 7) == 0)
    {
        printf("STATUS RX -> %s\n", msg);
    }

    cJSON_Delete(root);
}

static void heartbeat_worker_fn(async_context_t *context, async_at_time_worker_t *worker)
{
    (void)context;

    static uint32_t counter = 0;

    char msg[32];

    snprintf(msg, sizeof(msg), "alive:%lu", counter++);

    mqtt_client_pub("/heartbeat", msg);

    async_context_add_at_time_worker_in_ms(
        cyw43_arch_async_context(),
        worker,
        1000);
}

int main()
{
    stdio_init_all();

    sleep_ms(500);

    snprintf(g_cmd_topic, sizeof(g_cmd_topic), "/cmd/%s", g_token);

    snprintf(g_ack_topic, sizeof(g_ack_topic), "/ack/%s", g_token);

    snprintf(g_data_topic, sizeof(g_data_topic), "/data/%s", g_token);

    if (!init_wifi_sta())
    {
        printf("WiFi init failed\n");
        return -1;
    }

    if (!wifi_connect(NULL, NULL))
    {
        printf("WiFi connect failed\n");
    }

    if (wifi_is_connected())
    {
        sleep_ms(500);
        mqtt_client_config_t mqtt_cfg = {
            .client_id = "pico_w",

#ifdef MQTT_USERNAME
            .username = MQTT_USERNAME,
            .password = MQTT_PASSWORD,
#else
            .username = NULL,
            .password = NULL,
#endif

            // .server = "MQTT_SERVER",
            .server = "broker.emqx.io",
            .port = 1883,
            .keep_alive = 60,
            .data_cb = my_mqtt_cb,
        };

        init_mqtt_client(&mqtt_cfg);

        heartbeat_worker.user_data = NULL;

        mqtt_register_periodic_worker(&heartbeat_worker, 1000);
    }

    init_led(&heart_beat_led);

    led_start_blink(&heart_beat_led, 1000);

    init_rgb_led(&rgb);

    init_pot(POT_GPIO);

    servo_driver_init();
    servo1 = servo_attach(SERVO_GPIO);

    init_buttons();

    uart_config_t uart_cfg = {
        .uart = uart0,
        .tx_pin = 0,
        .rx_pin = 1,
        .baudrate = 115200,
        .data_bits = 8,
        .stop_bits = 1,
        .parity = UART_PARITY_NONE,
        .hw_flow = false,
        .rx_event_callback = NULL,
    };

    uart_driver_init(&cli_uart, &uart_cfg);

    init_serial_parser(&parser);

    bool mqtt_subscribed = false;

    static float priv_servo_angle = 0.0f;

    printf("System Ready\n");

    while (1)
    {
        wifi_poll();

        mqtt_client_poll();

        if (!mqtt_is_connected())
        {
            if (!mqtt_is_connecting())
            {
                mqtt_subscribed = false;

                if (absolute_time_diff_us(
                        last_mqtt_retry,
                        get_absolute_time()) >
                    MQTT_RETRY_INTERVAL_MS * 1000)
                {
                    printf("MQTT reconnect attempt...\n");

                    last_mqtt_retry = get_absolute_time();

                    reinit_mqtt_client();
                }
            }
        }

        if (mqtt_is_connected() && !mqtt_subscribed)
        {
            mqtt_client_sub_topic(g_ack_topic);

            mqtt_client_sub_topic(g_data_topic);

            mqtt_subscribed = true;

            printf("Subscribed\n");
        }

        uint8_t byte;

        while (uart_driver_read_byte(&cli_uart, &byte))
        {
            if (serial_parser_feed(&parser, byte))
            {
                printf("UART RX: %s\n", parser.packet.data);

                cJSON *root = parse_serial_to_json((const char *)parser.packet.data, parser.packet.len);

                if (root)
                {
                    char *json = cJSON_PrintUnformatted(root);

                    if (json)
                    {
                        printf("MQTT TX [%s]: %s\n", g_cmd_topic, json);

                        mqtt_client_pub(g_cmd_topic, json);

                        free(json);
                    }

                    cJSON_Delete(root);
                }

                serial_parser_reset(&parser);
            }
        }

        if (absolute_time_diff_us(last_pot_read, get_absolute_time()) >= 100000)
        {
            last_pot_read = get_absolute_time();

            uint32_t pot_read = pot_read_mapped(0, 180);

            current_servo_angle = (float)pot_read;

            if (abs((int)(priv_servo_angle - current_servo_angle)) > 5)
            {
                priv_servo_angle = current_servo_angle;

                char cmd[64];

                snprintf(cmd, sizeof(cmd), "SERVO:%d", (int)current_servo_angle);

                cJSON *root = parse_serial_to_json(cmd, strlen(cmd));

                if (root)
                {
                    char *json = cJSON_PrintUnformatted(root);

                    if (json)
                    {
                        printf("MQTT TX [%s]: %s\n", g_cmd_topic, json);

                        mqtt_client_pub(g_cmd_topic, json);

                        free(json);
                    }

                    cJSON_Delete(root);
                }
            }
        }

        rgb_led_update(&rgb);

        tight_loop_contents();
    }

    return 0;
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
        button_config_t btn_cfg =
            {
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

static void button_callback(uint gpio, button_event_t event, void *user_data)
{
    (void)user_data;

    if (event == BUTTON_EVENT_SHORT_PRESS)
    {
        printf("GPIO %u SHORT PRESS\n", gpio);
    }
}