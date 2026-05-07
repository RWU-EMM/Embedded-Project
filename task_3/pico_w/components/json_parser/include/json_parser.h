#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include <stdbool.h>
#include <stdint.h>
// Parsed structure
typedef struct
{
    char token[32];
    uint32_t seq;

    struct
    {
        int en;
        int blink_intvl;
    } led;

    struct
    {
        int en;
        int blink_intvl;
        int led_idx;
        uint8_t r;
        uint8_t g;
        uint8_t b;
#if RGB_LED_USE_RGBW
    uint8_t w;
#endif
    } rgb;

    uint8_t pot_mode;

    char spl_cmd[32];

} json_cmd_t;

// Parse incoming JSON
bool json_parse_command(const char *json_str, json_cmd_t *cmd);

// Create ACK JSON
bool json_create_ack(char *buffer, int buffer_size, int seq);

// Create generic JSON publish (helper)
bool json_create_simple(char *buffer, int buffer_size,
                        const char *key, const char *value);

#endif // JSON_PARSER_H