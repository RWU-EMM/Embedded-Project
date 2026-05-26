#include "json_parser.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

// Parse incoming JSON
bool json_parse_command(const char *json_str, json_cmd_t *cmd)
{
    if (!json_str || !cmd)
        return false;

    cJSON *root = cJSON_Parse(json_str);
    if (!root)
        return false;

    // token
    cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "token");
    if (cJSON_IsString(token) && token->valuestring)
    {
        strncpy(cmd->token, token->valuestring, sizeof(cmd->token) - 1);
    }

    // seq
    cJSON *seq = cJSON_GetObjectItemCaseSensitive(root, "seq");
    if (cJSON_IsNumber(seq))
    {
        cmd->seq = seq->valueint;
    }

    // LED
    cJSON *led = cJSON_GetObjectItemCaseSensitive(root, "led");
    if (cJSON_IsObject(led))
    {
        cJSON *en = cJSON_GetObjectItemCaseSensitive(led, "en");
        cJSON *blink = cJSON_GetObjectItemCaseSensitive(led, "blink_intvl");

        if (cJSON_IsNumber(en)){
            cmd->led.en = en->valueint;
        }

        if (cJSON_IsNumber(blink))
        {
            // bink range to 50 to 2000
            blink->valueint = (blink->valueint <= 50) ? 50 : blink->valueint;
            blink->valueint = (blink->valueint >= 2000) ? 2000 : blink->valueint;
            cmd->led.blink_intvl = blink->valueint;
        }
    }

    // POT (flat)
    cJSON *pot = cJSON_GetObjectItemCaseSensitive(root, "pot");
    if (cJSON_IsNumber(pot))
    {
        if (pot->valueint == 0 || pot->valueint == 1)
            cmd->pot_mode = (uint8_t)pot->valueint;
        else
        {
            cJSON_Delete(root);
            return false;
        }
    }

    // RGB
    cJSON *rgb = cJSON_GetObjectItemCaseSensitive(root, "rgb");
    if (cJSON_IsObject(rgb))
    {
        cJSON *en = cJSON_GetObjectItemCaseSensitive(rgb, "en");
        cJSON *blink = cJSON_GetObjectItemCaseSensitive(rgb, "blink_intvl");
        cJSON *idx = cJSON_GetObjectItemCaseSensitive(rgb, "led_idx");
        cJSON *r = cJSON_GetObjectItemCaseSensitive(rgb, "r");
        cJSON *g = cJSON_GetObjectItemCaseSensitive(rgb, "g");
        cJSON *b = cJSON_GetObjectItemCaseSensitive(rgb, "b");
#if RGB_LED_USE_RGBW
        cJSON *w = cJSON_GetObjectItemCaseSensitive(rgb, "w");
#endif

        if (cJSON_IsNumber(en))
            cmd->rgb.en = en->valueint;

        if (cJSON_IsNumber(blink))
            cmd->rgb.blink_intvl = blink->valueint;

        if (cJSON_IsNumber(idx))
            cmd->rgb.led_idx = idx->valueint;

        if (cJSON_IsNumber(r))
            cmd->rgb.r = (uint8_t)r->valueint;

        if (cJSON_IsNumber(g))
            cmd->rgb.g = (uint8_t)g->valueint;

        if (cJSON_IsNumber(b))
            cmd->rgb.b = (uint8_t)b->valueint;

#if RGB_LED_USE_RGBW
        if (cJSON_IsNumber(w))
            cmd->rgb.w = (uint8_t)b->valueint;
#endif
    }

    // special command
    cJSON *spl = cJSON_GetObjectItemCaseSensitive(root, "spl_cmd");
    if (cJSON_IsString(spl) && spl->valuestring)
    {
        strncpy(cmd->spl_cmd, spl->valuestring, sizeof(cmd->spl_cmd) - 1);
    }

    cJSON_Delete(root);
    return true;
}

// json_create_ack: Create ACK JSON
bool json_create_ack(char *buffer, int buffer_size, int seq)
{
    return snprintf(buffer, buffer_size, "{\"ack\":%d}", seq) > 0;
}

// CREATE SIMPLE JSON
bool json_create_simple(char *buffer, int buffer_size,
                        const char *key, const char *value)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return false;

    cJSON_AddStringToObject(root, key, value);

    char *str = cJSON_PrintUnformatted(root);
    if (!str)
    {
        cJSON_Delete(root);
        return false;
    }

    strncpy(buffer, str, buffer_size);
    buffer[buffer_size - 1] = '\0';

    cJSON_free(str);
    cJSON_Delete(root);

    return true;
}
