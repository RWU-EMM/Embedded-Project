#include "serial_parser.h"

#include "pico/stdlib.h"

#include <string.h>

void init_serial_parser(serial_parser_t *parser)
{
    if (!parser)
    {
        return;
    }

    memset(parser, 0, sizeof(serial_parser_t));
}

void serial_parser_reset(serial_parser_t *parser)
{
    if (!parser)
    {
        return;
    }

    parser->index = 0;

    parser->packet.len = 0;

    parser->packet.packet_ready = false;

    memset(parser->packet.data, 0, serial_MAX_LEN);
}

bool serial_parser_feed(
    serial_parser_t *parser,
    uint8_t byte)
{
    if (!parser)
    {
        return false;
    }

    /*
     * Ignore CR
     */

    if (byte == '\r')
    {
        return false;
    }

    /*
     * End of packet
     */

    if (byte == '\n')
    {
        parser->packet.data[parser->index] = '\0';

        parser->packet.len = parser->index;

        parser->packet.packet_ready = true;

        parser->index = 0;

        return true;
    }

    /*
     * Prevent overflow
     */

    if (parser->index >= (serial_MAX_LEN - 1))
    {
        serial_parser_reset(parser);

        return false;
    }

    parser->packet.data[parser->index++] = byte;

    return false;
}