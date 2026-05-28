#include "json_parser.h"

#include "cJSON.h"

#include <string.h>

bool json_parse(const char *data, uint16_t len, json_packet_t *pkt)
{
    if (!data || !len || !pkt)
    {
        return false;
    }

    memset(pkt, 0, sizeof(*pkt));

    cJSON *root = cJSON_ParseWithLength(data, len);

    if (!root)
    {
        return false;
    }

    cJSON *token = cJSON_GetObjectItem(root, "token");
    cJSON *source = cJSON_GetObjectItem(root, "source");
    cJSON *seq = cJSON_GetObjectItem(root, "seq");
    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");

    if (!cJSON_IsString(token) || !token->valuestring)
    {
        cJSON_Delete(root);
        return false;
    }

    if (!cJSON_IsString(source) || !source->valuestring)
    {
        cJSON_Delete(root);
        return false;
    }

    if (!cJSON_IsNumber(seq))
    {
        cJSON_Delete(root);
        return false;
    }

    if (!cJSON_IsString(cmd) || !cmd->valuestring)
    {
        cJSON_Delete(root);
        return false;
    }

    if (strcmp(token->valuestring, "iem2026") != 0)
    {
        cJSON_Delete(root);
        return false;
    }

    strncpy(pkt->token, token->valuestring, sizeof(pkt->token) - 1);
    strncpy(pkt->source, source->valuestring, sizeof(pkt->source) - 1);
    strncpy(pkt->cmd, cmd->valuestring, sizeof(pkt->cmd) - 1);

    pkt->seq = seq->valueint;

    cJSON_Delete(root);

    return true;
}