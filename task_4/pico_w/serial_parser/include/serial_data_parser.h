#ifndef SERIAL_DATA_PARSER_H
#define SERIAL_DATA_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#define SERIAL_DATA_MAX_LEN 256

typedef struct
{
    uint8_t data[SERIAL_DATA_MAX_LEN];
    uint16_t len;
    bool packet_ready;
} serial_data_pkt_t;

typedef struct
{
    serial_data_pkt_t packet;
    uint16_t index;
} serial_data_parser_t;

/* API */

void init_serial_data_parser(serial_data_parser_t *parser);

bool serial_data_parser_feed(serial_data_parser_t *parser, uint8_t byte);

void serial_data_parser_reset(serial_data_parser_t *parser);

#endif // SERIAL_DATA_PARSER_H