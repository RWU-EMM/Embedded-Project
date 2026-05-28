#ifndef SERIAL_DATA_PARSER_H
#define SERIAL_DATA_PARSER_H

#include <stdint.h>
#include "cJSON.h"

cJSON *parse_serial_to_json(const char *data, int len);

#endif // SERIAL_DATA_PARSER_H