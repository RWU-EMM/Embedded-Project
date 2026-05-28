#ifndef SERIAL_DATA_PARSER_H
#define SERIAL_DATA_PARSER_H

#include <stdint.h>

#include "esp_err.h"

#include "cJSON.h"

cJSON *serial_data_parse_to_json(const char *data, int len);

#endif // SERIAL_DATA_PARSER_H