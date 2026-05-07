#include "device_handler.h"
#include "device_registry.h"
#include "auth_handler.h"
#include "mqtt_manager.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "cJSON.h"
#include "esp_log.h"

#include "esp_timer.h"

static uint8_t extract_token(const char *topic, const char *prefix, char *out, size_t len)
{
    size_t plen = strlen(prefix);

    if (strncmp(topic, prefix, plen) != 0)
        return 0;

    snprintf(out, len, "%s", topic + plen);
    return 1;
}

static void handle_ack(const char *topic, const char *data, int len)
{
    char token[32];

    if (!extract_token(topic, "/ack/", token, sizeof(token)))
        return;

    device_entry_t *dev = device_find_by_token(token);
    if (!dev)
        return;

    char *buf = strndup(data, len);
    cJSON *root = cJSON_Parse(buf);
    free(buf);

    if (!root)
        return;

    cJSON *ack = cJSON_GetObjectItem(root, "ack");

    if (cJSON_IsNumber(ack))
    {
        uint32_t seq = ack->valueint;

        if (seq == dev->seq_tx)
        {
            device_update_ack_and_time(dev, seq);
            ESP_LOGI("ACK", "OK %s seq=%lu", token, seq);
        }
        else
        {
            ESP_LOGW("ACK", "Mismatch %s seq=%lu", token, seq);
        }
    }

    cJSON_Delete(root);
}

static void handle_data(const char *topic,
                        const char *data,
                        int len)
{
    /*
     * topic format:
     * /data/<token>
     */

    const char *token = topic + 6;

    ESP_LOGI(__func__, "DATA [%s]: %.*s", token, len, data);

    /*
     * Find device
     */

    device_entry_t *dev = device_find_by_token(token);

    if (!dev)
    {
        ESP_LOGW(__func__, "Unknown device token: %s", token);
        return;
    }

    /*
     * Make MQTT payload NULL terminated
     */

    char *buf = strndup(data, len);

    if (!buf)
    {
        ESP_LOGE(__func__,
                 "Memory allocation failed");
        return;
    }

    /*
     * Parse JSON
     */

    cJSON *root = cJSON_Parse(buf);

    free(buf);

    if (!root)
    {
        ESP_LOGW(__func__,
                 "Invalid JSON from %s",
                 token);
        return;
    }

    /*
     * STATUS RESPONSE PARSING
     *
     * Expected:
     *
     * {
     *   "led":{
     *      "en":1,
     *      "blink_intvl":500
     *   },
     *   "pot":1
     * }
     */

    cJSON *led = cJSON_GetObjectItem(root, "led");

    if (cJSON_IsObject(led))
    {
        cJSON *blink = cJSON_GetObjectItem(led, "blink_intvl");

        if (cJSON_IsNumber(blink))
        {
            dev->mirror_blink_ms =
                blink->valueint;

            ESP_LOGI(__func__,
                     "Mirror blink updated: %lu ms",
                     dev->mirror_blink_ms);
        }

        cJSON *en = cJSON_GetObjectItem(led, "en");

        if (cJSON_IsNumber(en))
        {
            ESP_LOGI(__func__, "LED state = %d", en->valueint);
        }
    }

    /*
     * Optional POT status
     */

    cJSON *pot =
        cJSON_GetObjectItem(root, "pot");

    if (cJSON_IsNumber(pot))
    {
        ESP_LOGI(__func__, "POT mode = %d", pot->valueint);
    }

    /*
     * Optional uptime stats
     */

    cJSON *uptime =
        cJSON_GetObjectItem(root,
                            "uptime_ms");

    if (cJSON_IsNumber(uptime))
    {
        ESP_LOGI(__func__,
                 "Uptime = %lu ms",
                 (uint32_t)uptime->valuedouble);
    }

    /*
     * Optional wifi/mqtt stats
     */

    cJSON *wifi = cJSON_GetObjectItem(root, "wifi");
    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
    cJSON *seq = cJSON_GetObjectItem(root, "seq");

    if (cJSON_IsNumber(wifi) && cJSON_IsNumber(mqtt))
    {
        ESP_LOGI(__func__, "Remote WiFi=%d", wifi->valueint);
    }
    if (cJSON_IsNumber(mqtt))
    {
        ESP_LOGI(__func__, "Remote MQTT=%d", mqtt->valueint);
    }
    if (cJSON_IsNumber(seq))
    {
        ESP_LOGI(__func__, "Remote SEQ=%d", seq->valueint);
    }

    cJSON_Delete(root);
}

static void handle_auth(const char *topic, const char *data, int len)
{
    char *buf = strndup(data, len);

    auth_handle_response(topic, buf);

    if (auth_is_done())
    {
        const char *token = auth_get_token();

        device_mark_authenticated(token);

        char t_ack[64], t_data[64];

        snprintf(t_ack, sizeof(t_ack), "/ack/%s", token);
        snprintf(t_data, sizeof(t_data), "/data/%s", token);

        mqtt_service_subscribe(t_ack, 1);
        mqtt_service_subscribe(t_data, 1);

        ESP_LOGI(__func__, "Device %s READY", token);
    }

    free(buf);
}

void init_device_handler(void)
{
    init_auth();
    init_device_registry();

    mqtt_service_register_handler("/ack/#", handle_ack);
    mqtt_service_register_handler("/data/#", handle_data);

    mqtt_service_register_handler("/ack", handle_auth);
    mqtt_service_register_handler("/connect", handle_auth);

    ESP_ERROR_CHECK(mqtt_service_start());

    mqtt_service_subscribe("/ack", 1);
    mqtt_service_subscribe("/connect", 1);
    mqtt_service_subscribe("/ack/#", 1);
    mqtt_service_subscribe("/data/#", 1);
}

void device_connect(const char *token, uint8_t bypass)
{
    ESP_LOGI(__func__, "Auth start: %s", token);
    auth_start(token, bypass); // bypass = true (your design)
}

static void parse_kv_and_apply(cJSON *obj, const char *key, const char *val)
{
    if (!key || !val)
        return;

    if (isdigit((unsigned char)val[0]))
    {
        cJSON_AddNumberToObject(obj, key, atoi(val));
    }
    else
    {
        cJSON_AddStringToObject(obj, key, val);
    }
}

static bool rgb_from_color(const char *color,
                           int *r,
                           int *g,
                           int *b)
{
    if (!color || !r || !g || !b)
        return false;

    if (strcasecmp(color, "RED") == 0)
    {
        *r = 255;
        *g = 0;
        *b = 0;
    }
    else if (strcasecmp(color, "GREEN") == 0)
    {
        *r = 0;
        *g = 255;
        *b = 0;
    }
    else if (strcasecmp(color, "BLUE") == 0)
    {
        *r = 0;
        *g = 0;
        *b = 255;
    }
    else if (strcasecmp(color, "WHITE") == 0)
    {
        *r = 255;
        *g = 255;
        *b = 255;
    }
    else if (strcasecmp(color, "YELLOW") == 0)
    {
        *r = 255;
        *g = 255;
        *b = 0;
    }
    else if (strcasecmp(color, "CYAN") == 0)
    {
        *r = 0;
        *g = 255;
        *b = 255;
    }
    else if (strcasecmp(color, "PURPLE") == 0)
    {
        *r = 255;
        *g = 0;
        *b = 255;
    }
    else
    {
        return false;
    }

    return true;
}

void device_send_command(const char *cmd)
{
    char buf[256];
    strncpy(buf, cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *saveptr;
    char *token = strtok_r(buf, ":", &saveptr);

    if (!token)
    {
        ESP_LOGW(__func__, "Missing token");
        return;
    }

    device_entry_t *dev = device_find_by_token(token);

    if (!dev)
    {
        ESP_LOGW(__func__, "Device [%s] not found", token);
        return;
    }

    if (!dev->authenticated)
    {
        ESP_LOGW(__func__, "Device [%s] not authenticated", token);
        return;
    }

    device_update_tx(dev);

    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "token", token);
    cJSON_AddNumberToObject(root, "seq", dev->seq_tx);

    cJSON *led = NULL;
    cJSON *rgb = NULL;

    char *current = strtok_r(NULL, ":", &saveptr);

    while (current)
    {
        /*
         * LED BLOCK
         *  */
        if (strcmp(current, "LED") == 0)
        {
            if (!led)
                led = cJSON_CreateObject();

            while ((current = strtok_r(NULL, ":", &saveptr)))
            {
                if (strcmp(current, "RGB") == 0 ||
                    strcmp(current, "CMD") == 0 ||
                    strcmp(current, "LED") == 0 ||
                    strncmp(current, "POT=", 4) == 0)
                    break;

                char *eq = strchr(current, '=');
                if (!eq)
                    continue;

                *eq = '\0';

                char *key = current;
                char *val = eq + 1;

                if (strcmp(key, "EN") == 0)
                {
                    parse_kv_and_apply(led, "en", val);
                }
                else if (strcmp(key, "BLINK") == 0)
                {
                    parse_kv_and_apply(led, "blink_intvl", val);
                }
            }

            continue;
        }

        /*
         * RGB BLOCK
         */
        if (strcmp(current, "RGB") == 0)
        {
            if (!rgb)
                rgb = cJSON_CreateObject();

            while ((current = strtok_r(NULL, ":", &saveptr)))
            {
                if (strcmp(current, "LED") == 0 ||
                    strcmp(current, "CMD") == 0 ||
                    strcmp(current, "RGB") == 0 ||
                    strncmp(current, "POT=", 4) == 0)
                    break;

                char *eq = strchr(current, '=');

                if (!eq)
                    continue;

                *eq = '\0';

                char *key = current;
                char *val = eq + 1;

                int num = atoi(val);

                if (strcmp(key, "EN") == 0)
                {
                    cJSON_AddNumberToObject(rgb, "en", num);
                }
                else if (strcmp(key, "BLINK") == 0)
                {
                    num = (num <= 50) ? 50 : num;
                    num = (num >= 2000) ? 2000 : num;

                    cJSON_AddNumberToObject(rgb,
                                            "blink_intvl",
                                            num);
                }
                else if (strcmp(key, "IDX") == 0)
                {
                    cJSON_AddNumberToObject(rgb,
                                            "led_idx",
                                            num);
                }
                else if (strcmp(key, "COLOR") == 0)
                {
                    int r, g, b;

                    if (rgb_from_color(val, &r, &g, &b))
                    {
                        cJSON_AddNumberToObject(rgb, "r", r);
                        cJSON_AddNumberToObject(rgb, "g", g);
                        cJSON_AddNumberToObject(rgb, "b", b);
                    }
                    else
                    {
                        cJSON_AddNumberToObject(rgb, "r", 100);
                        cJSON_AddNumberToObject(rgb, "g", 100);
                        cJSON_AddNumberToObject(rgb, "b", 100);
                        ESP_LOGW(__func__,
                                 "Unknown color: %s",
                                 val);
                    }
                }
                else if (strcmp(key, "R") == 0)
                {
                    cJSON_AddNumberToObject(rgb, "r", num);
                }
                else if (strcmp(key, "G") == 0)
                {
                    cJSON_AddNumberToObject(rgb, "g", num);
                }
                else if (strcmp(key, "B") == 0)
                {
                    cJSON_AddNumberToObject(rgb, "b", num);
                }

#if RGB_LED_USE_RGBW
                else if (strcmp(key, "W") == 0)
                {
                    cJSON_AddNumberToObject(rgb, "w", num);
                }
#endif
            }

            continue;
        }
        /*
         * POT
         *  */
        if (strncmp(current, "POT=", 4) == 0)
        {
            char *eq = strchr(current, '=');
            if (!eq)
                continue;

            *eq = '\0';

            char *val = eq + 1;
            parse_kv_and_apply(root, "pot", val);
            current = strtok_r(NULL, ":", &saveptr);
            continue;
        }
        /*
         * SPECIAL CMD
         *  */
        if (strcmp(current, "CMD") == 0)
        {
            char *sub = strtok_r(NULL, ":", &saveptr);

            if (sub)
                cJSON_AddStringToObject(root, "spl_cmd", sub);

            current = strtok_r(NULL, ":", &saveptr);
            continue;
        }

        current = strtok_r(NULL, ":", &saveptr);
    }

    /* attach objects only if used */
    if (led)
        cJSON_AddItemToObject(root, "led", led);

    if (rgb)
        cJSON_AddItemToObject(root, "rgb", rgb);

    /*
     * SEND
     *  */

    char *msg = cJSON_PrintUnformatted(root);

    mqtt_service_publish("/cmd", msg, 0, 0);

    ESP_LOGI(__func__, "TX %s seq=%lu -> %s",
             token,
             dev->seq_tx,
             msg);

    free(msg);
    cJSON_Delete(root);
}