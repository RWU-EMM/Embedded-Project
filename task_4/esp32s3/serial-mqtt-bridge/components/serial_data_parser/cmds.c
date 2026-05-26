#include "cmds.h"
#include <stdio.h>

const field_desc_t led_fields[] = {
    {
        .name = "EN",
        .json_key = "en",
        .type = _FIELD_TYPE_INT,
        .ops = FIELD_OP_SET | FIELD_OP_GET,
        .max = 1,
        .min = 0,
    },
    {
        .name = "BLINK",
        .json_key = "blink_intvl",
        .type = _FIELD_TYPE_INT,
        .ops = FIELD_OP_SET | FIELD_OP_GET,
        .max = 1000,
        .min = 50,
    },
};

const cmd_desc_t led_cmd = {
    .name = "LED",
    .json_obj_name = "led",
    .fields = led_fields,
    .field_count = sizeof(led_fields) / sizeof(led_fields[0]),
};

const field_desc_t rgb_fields[] = {
    {
        .name = "EN",
        .json_key = "en",
        .type = _FIELD_TYPE_INT,
        .ops = FIELD_OP_SET | FIELD_OP_GET,
        .min = 0,
        .max = 1,
    },
    {
        .name = "BLINK",
        .json_key = "blink_intvl",
        .type = _FIELD_TYPE_INT,
        .ops = FIELD_OP_SET | FIELD_OP_GET,
        .min = 50,
        .max = 2000,
    },
    {
        .name = "R",
        .json_key = "r",
        .type = _FIELD_TYPE_INT,
        .ops = FIELD_OP_SET | FIELD_OP_GET,
        .min = 0,
        .max = 255,
    },
    {
        .name = "G",
        .json_key = "g",
        .type = _FIELD_TYPE_INT,
        .ops = FIELD_OP_SET | FIELD_OP_GET,
        .min = 0,
        .max = 255,
    },
    {
        .name = "B",
        .json_key = "b",
        .type = _FIELD_TYPE_INT,
        .ops = FIELD_OP_SET | FIELD_OP_GET,
        .min = 0,
        .max = 255,
    },
    {
        .name = "COLOR",
        .json_key = "color",
        .type = _FIELD_TYPE_COLOR,
        .ops = FIELD_OP_SET,
    },
};
const cmd_desc_t rgb_cmd = {
    .name = "RGB",
    .json_obj_name = "rgb",
    .fields = rgb_fields,
    .field_count = sizeof(rgb_fields) / sizeof(rgb_fields[0]),
};

const field_desc_t pot_fields[] = {
    {
        .name = "EN",
        .json_key = "en",
        .type = _FIELD_TYPE_INT,
        .ops = FIELD_OP_SET | FIELD_OP_GET,
        .min = 0,
        .max = 1,
    },
};

// pot
const cmd_desc_t pot_cmd = {
    .name = "POT",
    .json_obj_name = "pot",
    .fields = pot_fields,
    .field_count = sizeof(pot_fields) / sizeof(pot_fields[0]),
};

// servo
const field_desc_t servo_fields[] = {
    {
        .name = "EN",
        .json_key = "en",
        .type = _FIELD_TYPE_INT,
        .ops = FIELD_OP_SET | FIELD_OP_GET,
        .min = 0,
        .max = 1,
    },
    {
        .name = "ANGLE",
        .json_key = "ang",
        .type = _FIELD_TYPE_INT,
        .ops = FIELD_OP_SET | FIELD_OP_GET,
        .min = 0,
        .max = 180,
    },
};

const cmd_desc_t servo_cmd = {
    .name = "SERVO",
    .json_obj_name = "servo",
    .fields = servo_fields,
    .field_count = sizeof(servo_fields) / sizeof(servo_fields[0]),
};

const field_desc_t temp_fields[] = {
    {
        .name = "VAL",
        .json_key = "val",
        .type = _FIELD_TYPE_INT,
        .ops = FIELD_OP_GET,
    },
};
// temperature sensor
const cmd_desc_t temp_cmd = {
    .name = "TEMP",
    .json_obj_name = "temp",
    .fields = temp_fields,
    .field_count = sizeof(temp_fields) / sizeof(temp_fields[0]),
};

// special cmds

const field_desc_t spl_cmds_fields[] = {
    {

        .name = "STATS",
        .json_key = "stats",
        .type = _FIELD_TYPE_INT,
        .ops = FIELD_OP_SET,
        .min = 0,
        .max = 1,
    },
    {

        .name = "STATUS",
        .json_key = "status",
        .type = _FIELD_TYPE_INT,
        .ops = FIELD_OP_SET,
        .min = 0,
        .max = 1,
    },
    {

        .name = "HELP",
        .json_key = "help",
        .type = _FIELD_TYPE_INT,
        .ops = FIELD_OP_SET,
        .min = 0,
        .max = 1,
    },
};
const cmd_desc_t spl_cmds = {
    .name = "SPLCMD",
    .json_obj_name = "splcmd",
    .fields = spl_cmds_fields,
    .field_count = sizeof(spl_cmds_fields) / sizeof(spl_cmds_fields[0]),
};

const cmd_desc_t *g_cmds[] = {
    &led_cmd,
    &rgb_cmd,
    &pot_cmd,
    &servo_cmd,
    &temp_cmd,
    &spl_cmds,
};

const size_t g_cmd_count = sizeof(g_cmds) / sizeof(g_cmds[0]);