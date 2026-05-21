#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/unique_id.h"

#include "cJSON.h"

#include "led_handler.h"
#include "wifi_driver.h"
#include "mqtt_client.h"
#include "auth.h"
#include "json_parser.h"
#include "pot_driver.h"

#include "rgb_led.h"

#define SWAP_LED_WITH_RGB 0

#define MQTT_RETRY_INTERVAL_MS 10000

#define CMD_TIMEOUT_MS 10000

void handle_auth(const char *data);
void handle_cmd(const char *data);
static void build_topic(char *out, size_t len, const char *base, const char *token);
static void heartbeat_worker_fn(async_context_t *context, async_at_time_worker_t *worker);
static void handle_status_led(
#if (1 == SWAP_STATUS_LED_WITH_RGB)
    int led_idx, uint8_t r, uint8_t g, uint8_t b,
#if RGB_LED_USE_RGBW
    uint8_t w,
#endif
#endif
    int blink_interval);

// User LED (GREEN)
led_t user_led = {
    .pin_num = 3,
    .en_ctrl = 1,
    .hw_type = _LED_HW_GPIO,
};

// Status LED (RED)
led_t status_led = {
    .pin_num = 2,
    .en_ctrl = 1,
    .hw_type = _LED_HW_GPIO,
};

// Heartbeat LED (CYW43 internal)
led_t heart_beat_led = {
    .pin_num = CYW43_WL_GPIO_LED_PIN,
    .en_ctrl = 1,
    .hw_type = _LED_HW_CYW43,
};

rgb_led_t rgb = {
    .pin = 1,       // IMPORTANT: WS2812 data pin (NOT used by other LEDs)
    .led_count = 4, // number of LEDs in strip
    .order = RGB_ORDER_GRB,
};

uint8_t en_local_pot_mode = 1; // 0 or 1

static absolute_time_t last_mqtt_retry = {0};

static absolute_time_t last_cmd_time = {0};

// JSON command structure
json_cmd_t cmd;

static async_at_time_worker_t heartbeat_worker = {
    .do_work = heartbeat_worker_fn};

static void build_topic(char *out, size_t len, const char *base, const char *token)
{
    snprintf(out, len, "%s/%s", base, token);
}

void my_mqtt_cb(const char *topic, const char *data)
{
    if (strcmp(topic, "/connect") == 0)
    {
        handle_auth(data);
    }
    else if (strcmp(topic, "/cmd") == 0)
    {
        handle_cmd(data);
    }
    else
    {
        printf("APP RX [%s]: %s\n", topic, data);
    }
}
static void heartbeat_worker_fn(async_context_t *context, async_at_time_worker_t *worker)
{
    (void)context;

    static uint32_t counter = 0;

    // Publish heartbeat
    char msg[32];
    snprintf(msg, sizeof(msg), "alive:%lu", counter++);

    mqtt_client_pub("/heartbeat", msg);

    // Re-schedule after 1 sec
    async_context_add_at_time_worker_in_ms(
        cyw43_arch_async_context(),
        worker,
        1000);
}

static void handle_led_swap(
#if (1 == SWAP_LED_WITH_RGB)
    int led_idx, uint8_t r, uint8_t g, uint8_t b,
#if RGB_LED_USE_RGBW
    uint8_t w,
#endif
#else
    led_t *led,
#endif
    int blink_interval)
{

#if (1 == SWAP_LED_WITH_RGB)
    rgb_led_blink_pixel(&rgb, led_idx, r, g, b, blink_interval);
#else
    led_set_interval(led, blink_interval);
#endif
}

int main()
{
    stdio_init_all();

    printf("System Init...\n");

    // WiFi Init
    if (!init_wifi_sta())
    {
        printf("WiFi init failed\n");
        return -1;
    }

    if (!wifi_connect(NULL, NULL)) // fallback to default SSID: wokwiguest and no password
    {
        printf("WiFi connect failed\n");
    }

    if (wifi_is_connected())
    {

        mqtt_client_config_t mqtt_cfg = {
            .client_id = "picosim1",
#ifdef MQTT_USERNAME
            .username = MQTT_USERNAME,
            .password = MQTT_PASSWORD,
#else
            .username = NULL,
            .password = NULL,
#endif
            .server = MQTT_SERVER_IP,
            .port = 1883,
            .keep_alive = 60,
            .data_cb = my_mqtt_cb};

        init_mqtt_client(&mqtt_cfg);

        heartbeat_worker.user_data = NULL;

        mqtt_register_periodic_worker(&heartbeat_worker, 1000);
    }

    init_led(&heart_beat_led);
    led_start_blink(&heart_beat_led, 1000);
    init_rgb_led(&rgb);

#if (1 == SWAP_LED_WITH_RGB)
    rgb_led_blink_pixel(&rgb, 0, 255, 250, 250, 250);
#else
    init_led(&status_led);
    led_start_blink(&status_led, 250); // Initially blink fast (connecting)
    init_led(&user_led);
    led_start_blink(&user_led, 250);
#endif

    auth_init();
    init_pot(26); // Initialize potentiometer on GPIO26
    static uint32_t last_pot = 0;

    // defualt values for cmd json structure
    cmd.token[0] = '\0';
    cmd.seq = 0;
    cmd.led.en = 1;
    cmd.led.blink_intvl = 500;
    cmd.pot_mode = 1;
    cmd.spl_cmd[0] = '\0';
    cmd.rgb.en = 0;
    cmd.rgb.blink_intvl = 0;

    int last_interval = 0; // track last LED state
    srand(to_ms_since_boot(get_absolute_time()));
    bool mqtt_subscribed = false;
    static uint32_t last_user_led_interval = 0;

    last_cmd_time = get_absolute_time();

    // Main Loop
    while (1)
    {
        // REQUIRED for WiFi stack
        wifi_poll();
        // REQUIRED for mqtt stack
        mqtt_client_poll();
        // Process Authenticatoin timeout
        auth_process();

        int interval;

        if (!wifi_is_connected())
        {
            // No WiFi
            interval = 250;
        }
        else if (!mqtt_is_connected())
        {
            interval = 500;
            if (!mqtt_is_connecting())
            {
                // WiFi connected, MQTT not yet
                mqtt_subscribed = false; // reset subscription state when MQTT disconnects
                en_local_pot_mode = 1;   // reset to default mode when MQTT disconnects

                if (absolute_time_diff_us(last_mqtt_retry, get_absolute_time()) > MQTT_RETRY_INTERVAL_MS * 1000)
                {
                    printf("MQTT reconnect attempt...\n");

                    last_mqtt_retry = get_absolute_time();

                    reinit_mqtt_client();
                }
            }
        }
        else
        {
            // MQTT connected
            interval = 1000;
        }

        // Only update LED if interval changed
        if (interval != last_interval)
        {

            last_interval = interval;

            handle_led_swap(
#if (1 == SWAP_LED_WITH_RGB)
                0, 0, 200, 0,
#if RGB_LED_USE_RGBW
                uint8_t w,
#endif
#else
                &status_led,
#endif
                interval);

            printf("LED interval set to %d ms\n", interval);
        }

        if (mqtt_is_connected() && !mqtt_subscribed)
        {
            mqtt_client_sub_topic(TOPIC_CONNECT);
            mqtt_client_sub_topic(TOPIC_CMD);

            mqtt_subscribed = true;

            printf("Subscribed to topics\n");
        }

        // Command timeout fallback, If no command received for 10 sec,restore local POT control
        if (absolute_time_diff_us(last_cmd_time,
                                  get_absolute_time()) > (CMD_TIMEOUT_MS * 1000))
        {
            if (en_local_pot_mode == 0)
            {
                en_local_pot_mode = 1;

                printf("CMD timeout -> Local POT mode restored\n");
            }
        }

        if (en_local_pot_mode == 1)
        {
            uint32_t temp_pot_val = pot_read_mapped(100, 2000);
            uint32_t pot_val = ((temp_pot_val + 5) / 10) * 10;
            if (pot_val != last_pot)
            {
                last_pot = pot_val;
                user_led.blink_interval_ms = pot_val;
                printf("POT Interval: %lu ms\n", pot_val);
            }
        }

        if (user_led.blink_interval_ms != last_user_led_interval)
        {
            last_user_led_interval = user_led.blink_interval_ms;
            printf("len en_ctrl = %d\n", user_led.en_ctrl);

            handle_led_swap(
#if (1 == SWAP_LED_WITH_RGB)
                1, 200, 0, 0,
#if RGB_LED_USE_RGBW
                uint8_t w,
#endif
#else
                &user_led,
#endif
                user_led.blink_interval_ms);

            printf("User LED Blink Interval updated: %d ms\n", user_led.blink_interval_ms);
        }

        tight_loop_contents();
        rgb_led_update(&rgb);
        sleep_ms(10);
    }
}

void handle_auth(const char *data)
{
    cJSON *root = cJSON_Parse(data);
    if (!root)
        return;

    // STEP 1
    cJSON *in_key = cJSON_GetObjectItem(root, "in_key");
    if (cJSON_IsString(in_key))
    {
        char out_key[16];

        if (auth_start(in_key->valuestring, out_key))
        {
            char resp[64];
            snprintf(resp, sizeof(resp),
                     "{\"out_key\":\"%s\"}", out_key);

            mqtt_client_pub(TOPIC_ACK, resp);
        }
        else
        {
            mqtt_client_pub(TOPIC_ACK,
                            "{\"status\":\"no_session\"}");
        }

        goto cleanup;
    }

    // @todo: if auth failed then restart auth process?
    // STEP 2
    cJSON *out_key = cJSON_GetObjectItem(root, "out_key");
    cJSON *reg = cJSON_GetObjectItem(root, "reg");
    cJSON *token = cJSON_GetObjectItem(root, "token");

    cJSON *bypass = cJSON_GetObjectItem(root, "bypass");

    if (cJSON_IsString(out_key) &&
        cJSON_IsString(reg) &&
        cJSON_IsString(token))
    {

        if (cJSON_IsNumber(bypass) && bypass->valueint == 1)
        {
            direct_token_registration(out_key->valuestring, token->valuestring);
            mqtt_client_pub(TOPIC_ACK, "{\"status\":\"ok\"}");
            goto cleanup;
        }

        if (auth_verify_and_register(out_key->valuestring, reg->valuestring, token->valuestring))
        {
            mqtt_client_pub(TOPIC_ACK, "{\"status\":\"ok\"}");
        }
        else
        {
            mqtt_client_pub(TOPIC_ACK, "{\"status\":\"fail\"}");
        }

        goto cleanup;
    }

    mqtt_client_pub(TOPIC_ACK,
                    "{\"status\":\"invalid_req\"}");
cleanup:
    cJSON_Delete(root);
}

void handle_cmd(const char *data)
{

    // @todo: nned better handling for new data and old data in cmd structure. Currently just overwrite, but if new data is partial then old data will be used which may cause unexpected behavior. Better to reset to default values and only update with new values from JSON.

    if (!json_parse_command(data, &cmd))
    {
        printf("Failed to parse JSON command\n");
        printf("%s\n", data);
        return;
    }

    char topic_ack[64];
    char topic_data[64];

    build_topic(topic_ack, sizeof(topic_ack), TOPIC_ACK, cmd.token);
    build_topic(topic_data, sizeof(topic_data), TOPIC_DATA, cmd.token);

    // AUTH CHECK

    if (!auth_is_token_valid(cmd.token))
    {
        mqtt_client_pub(TOPIC_ACK,
                        "{\"status\":\"unauthorized\"}");
        return;
    }

    last_cmd_time = get_absolute_time();

    printf("Bypassing authentication (for testing)\n");

    printf("Valid CMD seq=%d\n", cmd.seq);

    // EXECUTION
    // LED
    printf("LED en=%d, blink_intvl=%d \n", cmd.led.en, cmd.led.blink_intvl);
    user_led.en_ctrl = cmd.led.en;

    if (!user_led.en_ctrl)
    {

#if (1 == SWAP_LED_WITH_RGB)
        rgb_led_off_pixel(&rgb, 1);
#else
            led_off(&user_led);
#endif
        cmd.led.blink_intvl = 100; // reset to default interval when LED is turned off
    }

    user_led.blink_interval_ms = cmd.led.blink_intvl;

    // POT
    printf("POT mode=%d \n", cmd.pot_mode);
    en_local_pot_mode = cmd.pot_mode;

    // SPECIAL COMMANDS
    if (strcmp(cmd.spl_cmd, "STATUS") == 0)
    {
        char resp[128];

        snprintf(resp, sizeof(resp),
                 "{"
                 "\"led\":{\"en\":%d,\"blink_intvl\":%d},"
                 "\"pot\":%d"
                 "}",
                 user_led.en_ctrl,
                 user_led.blink_interval_ms,
                 en_local_pot_mode);

        mqtt_client_pub(topic_data, resp);
    }
    else if (strcmp(cmd.spl_cmd, "STATS") == 0)
    {
        char resp[128];

        uint32_t uptime = to_ms_since_boot(get_absolute_time());

        snprintf(resp, sizeof(resp),
                 "{"
                 "\"uptime_ms\":%lu,"
                 "\"wifi\":%d,"
                 "\"mqtt\":%d,"
                 "\"seq\":%lu"
                 "}",
                 uptime,
                 wifi_is_connected(),
                 mqtt_is_connected(),
                 cmd.seq);

        mqtt_client_pub(topic_data, resp);
    }
    else if (strcmp(cmd.spl_cmd, "HELP") == 0)
    {
        mqtt_client_pub(topic_data,
                        "{"
                        "\"cmd\":{"
                        "\"token\":\"string\","
                        "\"seq\":\"int\","
                        "\"led\":{"
                        "\"en\":\"0|1\","
                        "\"blink_intvl\":\"ms\""
                        "},"
                        "\"pot\":\"0:auto,1:manual\","
                        "\"spl_cmd\":[\"STATUS\",\"STATS\",\"HELP\"]"
                        "}"
                        "}");
    }
    // else if (cmd.spl_cmd[0] != '\0')
    // {
    //     mqtt_client_pub(topic_data,
    //                     "{\"error\":\"unknown_cmd\"}");
    // }
    cmd.spl_cmd[0] = '\0';

    // ACK
    char ack[64];
    if (json_create_ack(ack, sizeof(ack), cmd.seq))
    {
        mqtt_client_pub(topic_ack, ack);
    }
}