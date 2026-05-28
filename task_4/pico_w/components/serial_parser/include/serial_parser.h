#ifndef serial_PARSER_H
#define serial_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#define serial_MAX_LEN 256

typedef struct
{
    uint8_t data[serial_MAX_LEN];
    uint16_t len;
    bool packet_ready;
} serial_pkt_t;

typedef struct
{
    serial_pkt_t packet;
    uint16_t index;
} serial_parser_t;

/* API */

void init_serial_parser(serial_parser_t *parser);

bool serial_parser_feed(serial_parser_t *parser, uint8_t byte);

void serial_parser_reset(serial_parser_t *parser);

#endif // serial_PARSER_H