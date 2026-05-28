#include "serial_data_parser.h"

#include <stdio.h>
#include <string.h>

static uint32_t g_seq = 0;

cJSON *parse_serial_to_json(const char *data, int len)
{
    if (!len || !data)
    {
        return NULL;
    }

    char buf[512];

    if (len >= sizeof(buf))
    {
        printf("Input too large\n");
        return NULL;
    }

    memcpy(buf, data, len);

    buf[len] = '\0';

    //
    // trim trailing newline
    //
    size_t slen = strlen(buf);

    while (slen > 0)
    {
        char c = buf[slen - 1];

        if (c == '\n' || c == '\r' ||  c == ' ' || c == '\t')
        {
            buf[slen - 1] = '\0';
            slen--;
        }
        else
        {
            break;
        }
    }

    cJSON *root = cJSON_CreateObject();

    if (!root)
    {
        return NULL;
    }

    cJSON_AddStringToObject(root, "token", "iem2026");

    cJSON_AddStringToObject(root, "source", "pico-sim");

    cJSON_AddNumberToObject(root, "seq", g_seq++);

    cJSON_AddStringToObject(root, "cmd", buf);

    return root;
}