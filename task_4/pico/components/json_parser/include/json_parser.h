#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct json_packet
{
    char token[32];
    char source[32];

    uint32_t seq;

    char cmd[256];

} json_packet_t;

bool json_parse(const char *data, uint16_t len, json_packet_t *pkt);

#endif