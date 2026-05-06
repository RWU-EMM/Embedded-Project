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
            device_update_ack(dev, seq);
            ESP_LOGI("ACK", "OK %s seq=%lu", token, seq);
        }
        else
        {
            ESP_LOGW("ACK", "Mismatch %s seq=%lu", token, seq);
        }
    }

    cJSON_Delete(root);
}

static void handle_data(const char *topic, const char *data, int len)
{
    const char *token = topic + 6; // "/data/"

    ESP_LOGI(__func__, "DATA [%s]: %.*s", token, len, data);

    printf("DATA(%s): %.*s\n", token, len, data);
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
    if (!key || !val) return;

    if (isdigit((unsigned char)val[0])) {
        cJSON_AddNumberToObject(obj, key, atoi(val));
    } else {
        cJSON_AddStringToObject(obj, key, val);
    }
}


void device_send_command(const char *cmd)
{
    char buf[256];
    strncpy(buf, cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *saveptr;
    char *token = strtok_r(buf, ":", &saveptr);

    if (!token) {
        ESP_LOGW(__func__, "Missing token");
        return;
    }

    device_entry_t *dev = device_find_by_token(token);

    if (!dev || !dev->authenticated) {
        ESP_LOGW(__func__, "Device not ready");
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
                    strcmp(current, "LED") == 0)
                    break;

                char *eq = strchr(current, '=');
                if (!eq)
                    continue;

                *eq = '\0';

                char *key = current;
                char *val = eq + 1;

                if (strcmp(key, "EN") == 0)
                    parse_kv_and_apply(led, "en", val);

                else if (strcmp(key, "BLINK") == 0)
                    parse_kv_and_apply(led, "blink_intvl", val);
            }

            continue;
        }

        /* 
         * RGB BLOCK
         *  */
        if (strcmp(current, "RGB") == 0)
        {
            if (!rgb)
                rgb = cJSON_CreateObject();

            while ((current = strtok_r(NULL, ":", &saveptr)))
            {
                if (strcmp(current, "LED") == 0 ||
                    strcmp(current, "CMD") == 0 ||
                    strcmp(current, "RGB") == 0)
                    break;

                char *eq = strchr(current, '=');
                if (!eq)
                    continue;

                *eq = '\0';

                parse_kv_and_apply(rgb, current, eq + 1);
            }

            continue;
        }

        /* 
         * POT
         *  */
        if (strncmp(current, "POT=", 4) == 0)
        {
            int val = atoi(current + 4);

            if (val == 0 || val == 1)
                cJSON_AddNumberToObject(root, "pot", val);

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