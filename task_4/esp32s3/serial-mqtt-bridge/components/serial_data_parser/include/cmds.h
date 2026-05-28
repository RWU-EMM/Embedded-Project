#ifndef CMDS_H
#define CMDS_H

#include "cmd_types.h"

extern const cmd_desc_t led_cmd;
extern const cmd_desc_t rgb_cmd;
extern const cmd_desc_t pot_cmd;
extern const cmd_desc_t led_cmd;
extern const cmd_desc_t servo_cmd;
extern const cmd_desc_t temp_cmd;
extern const cmd_desc_t spl_cmds;

extern const cmd_desc_t *g_cmds[];
extern const size_t g_cmd_count;

#endif // CMDS_H