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

    if (cJSON_IsString(token) && token->valuestring)
    {
        strncpy(pkt->token, token->valuestring, sizeof(pkt->token) - 1);
    }

    cJSON *source = cJSON_GetObjectItem(root, "source");

    if (cJSON_IsString(source) && source->valuestring)
    {
        strncpy(pkt->source, source->valuestring, sizeof(pkt->source) - 1);
    }

    cJSON *seq = cJSON_GetObjectItem(root, "seq");

    if (cJSON_IsNumber(seq))
    {
        pkt->seq = seq->valueint;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");

    if (cJSON_IsString(cmd) && cmd->valuestring)
    {
        strncpy(pkt->cmd, cmd->valuestring, sizeof(pkt->cmd) - 1);
    }

    cJSON_Delete(root);

    return true;
}