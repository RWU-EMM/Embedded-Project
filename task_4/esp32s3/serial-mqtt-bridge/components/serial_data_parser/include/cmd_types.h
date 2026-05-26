#ifndef CMD_TYPES_H
#define CMD_TYPES_H

#include <stdint.h>
#include <string.h>

#define MAX_NAME_LENGTH 36
#define MAX_FIELD

typedef enum {
    _FIELD_TYPE_INT,
    _FIELD_TYPE_STRING,
    _FIELD_TYPE_BOOL,
    _FIELD_TYPE_COLOR,
} field_type_t;

typedef enum
{
    FIELD_OP_SET = (1 << 0),
    FIELD_OP_GET = (1 << 1),
} field_op_t;

typedef struct field_desc {
    const char name[MAX_NAME_LENGTH];
    const char json_key[MAX_NAME_LENGTH];
    field_type_t type;
    field_op_t ops;
    int max;
    int min;
}field_desc_t;

typedef struct cmd_desc
{
    const char name[MAX_NAME_LENGTH];
    const char json_obj_name[MAX_NAME_LENGTH];
    field_desc_t *fields;
    size_t field_count; // Number of fields in the command type
} cmd_desc_t;


#endif // CMD_TYPES_H