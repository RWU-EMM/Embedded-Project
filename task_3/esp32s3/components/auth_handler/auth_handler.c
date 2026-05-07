#include "auth_handler.h"
#include "mqtt_manager.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"


#include "esp_log.h"

#include "cJSON.h"


#define DEVICE_SECRET "PICO_SECRET"

static auth_state_t state = AUTH_IDLE;

static char token[32];
static char in_key[32];
static char out_key[32];

static uint32_t simple_hash(const char *str)
{
    uint32_t hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) ^ c;
    return hash;
}

static uint32_t generate_auth(const char *in, const char *out)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%s%s%s", in, out, DEVICE_SECRET);
    return simple_hash(buffer);
}


void init_auth(void)
{
    state = AUTH_IDLE;
    memset(token, 0, sizeof(token));
}

void auth_start(const char *dev_token, uint8_t bypass)
{
    strncpy(token, dev_token, sizeof(token)-1);
    strcpy(in_key, "HELLO123");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "in_key", in_key);

    if (bypass) {
        cJSON_AddNumberToObject(root, "bypass", 1);
    }

    char *msg = cJSON_PrintUnformatted(root);

    mqtt_service_publish("/connect", msg, 0, 0);
    
    free(msg);
    cJSON_Delete(root);

    state = AUTH_WAIT_OUT_KEY;
}

void auth_handle_response(const char *topic, const char *payload)
{
    if (strcmp(topic, "/ack") != 0 && strcmp(topic, "/connect") != 0)
        return;

    cJSON *root = cJSON_Parse(payload);
    if (!root) return;

    // SUCCESS
    cJSON *status = cJSON_GetObjectItem(root, "status");
    if (cJSON_IsString(status) && strcmp(status->valuestring, "ok") == 0) {
        state = AUTH_DONE;
        ESP_LOGI(__func__,"AUTH DONE");
        cJSON_Delete(root);
        return;
    }

    // STEP 2: receive out_key
    cJSON *out = cJSON_GetObjectItem(root, "out_key");
    if (state == AUTH_WAIT_OUT_KEY && cJSON_IsString(out)) {

        strcpy(out_key, out->valuestring);

        uint32_t reg = generate_auth(in_key, out_key);

        char reg_str[16];
        snprintf(reg_str, sizeof(reg_str), "%lu", reg);

        cJSON *req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "out_key", out_key);
        cJSON_AddStringToObject(req, "reg", reg_str);
        cJSON_AddStringToObject(req, "token", token);

        char *msg = cJSON_PrintUnformatted(req);

        // @todo: here mqtt publish should be done
       mqtt_service_publish("/connect", msg, 0, 0);

        free(msg);
        cJSON_Delete(req);

        state = AUTH_WAIT_VERIFY_ACK;
    }

    cJSON_Delete(root);
}

uint8_t auth_is_done(void)
{
    return (state == AUTH_DONE);
}

const char* auth_get_token(void)
{
    return token;
}