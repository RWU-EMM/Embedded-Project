#include "serial_data_parser.h"
#include "cmds.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_timer.h"

#include "esp_system.h"
#include "esp_log.h"

#define PARSER_GET_KEYWORD "GET"

typedef struct data_parser_ctx
{
    cJSON *root;
    const cmd_desc_t *current_cmd;
} data_parser_ctx_t;

static void trim_trailing_whitespace(char *str)
{
    if (!str)
    {
        return;
    }

    int len = strlen(str);

    while (len > 0)
    {
        char c = str[len - 1];

        if (c == '\r' ||
            c == '\n' ||
            c == ' ' ||
            c == '\t')
        {
            str[len - 1] = '\0';
            len--;
        }
        else
        {
            break;
        }
    }
}

static uint8_t get_rgb_from_color(const char *color, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!color || !r || !g || !b)
    {
        return 0;
    }
    if (strcasecmp(color, "RED") == 0)
    {
        *r = 255;
        *g = 0;
        *b = 0;
    }
    else if (strcasecmp(color, "GREEN") == 0)
    {
        *r = 0;
        *g = 255;
        *b = 0;
    }
    else if (strcasecmp(color, "BLUE") == 0)
    {
        *r = 0;
        *g = 0;
        *b = 255;
    }
    else if (strcasecmp(color, "WHITE") == 0)
    {
        *r = 255;
        *g = 255;
        *b = 255;
    }
    else if (strcasecmp(color, "YELLOW") == 0)
    {
        *r = 255;
        *g = 255;
        *b = 0;
    }
    else if (strcasecmp(color, "CYAN") == 0)
    {
        *r = 0;
        *g = 255;
        *b = 255;
    }
    else if (strcasecmp(color, "PURPLE") == 0)
    {
        *r = 255;
        *g = 0;
        *b = 255;
    }
    else
    {
        return 0;
    }

    return 1;
}

static uint8_t help_exists(cJSON *arr, const char *name)
{
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, arr)
    {
        if (cJSON_IsString(it) && it->valuestring &&
            strcmp(it->valuestring, name) == 0)
        {
            return 1;
        }
    }

    return 0;
}

static void register_help(data_parser_ctx_t *ctx, const char *cmd_name)
{
    cJSON *splcmd = cJSON_GetObjectItem(ctx->root, "splcmd");

    if (!splcmd)
    {
        splcmd = cJSON_CreateObject();

        cJSON_AddItemToObject(ctx->root, "splcmd", splcmd);
    }

    cJSON *help = cJSON_GetObjectItem(splcmd, "help");

    if (!help)
    {
        help = cJSON_AddArrayToObject(splcmd, "help");
    }

    //
    // ALL overrides everything
    //
    if (help_exists(help, "ALL"))
    {
        return;
    }

    if (!help_exists(help, cmd_name))
    {
        cJSON_AddItemToArray(help, cJSON_CreateString(cmd_name));
    }
}

static void register_all_help(data_parser_ctx_t *ctx)
{
    cJSON *splcmd = cJSON_GetObjectItem(ctx->root, "splcmd");

    if (!splcmd)
    {
        splcmd = cJSON_CreateObject();

        cJSON_AddItemToObject(ctx->root, "splcmd", splcmd);
    }

    //
    // remove old help array
    //
    cJSON_DeleteItemFromObject(splcmd, "help");

    cJSON *help = cJSON_AddArrayToObject(splcmd, "help");

    cJSON_AddItemToArray(help, cJSON_CreateString("ALL"));
}

static void apply_fields(cJSON *obj, const field_desc_t *field, const char *val)
{
    switch (field->type)
    {
    case _FIELD_TYPE_INT:
    {
        int num = atoi(val);
        if (num < field->min)
        {
            num = field->min;
        }

        if (num > field->max)
        {
            num = field->max;
        }
        cJSON_AddNumberToObject(obj, field->json_key, num);
        break;
    }
    case _FIELD_TYPE_BOOL:
    {
        int num = atoi(val);
        if (num < field->min)
        {
            num = field->min;
        }

        if (num > field->max)
        {
            num = field->max;
        }

        cJSON_AddNumberToObject(obj, field->json_key, num);
        break;
    }
    case _FIELD_TYPE_STRING:
    {
        cJSON_AddStringToObject(obj, field->json_key, val);
        break;
    }
    case _FIELD_TYPE_COLOR:
    {
        uint8_t r = 255, g = 0, b = 0;
        if (get_rgb_from_color(val, &r, &g, &b))
        {
            cJSON_AddNumberToObject(obj, "r", r);
            cJSON_AddNumberToObject(obj, "g", g);
            cJSON_AddNumberToObject(obj, "b", b);
        }
        else
        {
            ESP_LOGW("PARSING", "Invalid color setting COLOR=RED: %s", val);
            cJSON_AddNumberToObject(obj, "r", r);
            cJSON_AddNumberToObject(obj, "g", g);
            cJSON_AddNumberToObject(obj, "b", b);
        }
        break;
    }
    }
}

static const cmd_desc_t *find_cmd(const char *name)
{
    for (size_t i = 0; i < g_cmd_count; i++)
    {
        if (strcmp(g_cmds[i]->name, name) == 0)
        {
            return g_cmds[i];
        }
    }
    return NULL;
}

static const field_desc_t *find_field(const cmd_desc_t *cmd, const char *field_name)
{
    if (!cmd || !field_name)
    {
        return NULL;
    }

    for (size_t i = 0; i < cmd->field_count; i++)
    {
        if (strcmp(cmd->fields[i].name, field_name) == 0)
        {
            return &cmd->fields[i];
        }
    }

    return NULL;
}

static uint8_t req_exists(cJSON *arr, const char *name)
{
    cJSON *it = NULL;

    cJSON_ArrayForEach(it, arr)
    {
        if (cJSON_IsString(it) &&
            it->valuestring &&
            strcmp(it->valuestring, name) == 0)
        {
            return 1;
        }
    }

    return 0;
}

static void register_get_request(cJSON *obj, const char *field)
{
    cJSON *arr = cJSON_GetObjectItem(obj, "get");

    if (!arr)
    {
        arr = cJSON_AddArrayToObject(obj, "get");
    }

    if (!req_exists(arr, field))
    {
        cJSON_AddItemToArray(arr, cJSON_CreateString(field));
    }
}

cJSON *serial_data_parse_to_json(const char *data, int len)
{
    if (!data || !len)
    {
        return NULL;
    }

    char buf[512];
    if (len >= sizeof(buf))
    {
        ESP_LOGE(__func__, "Input too large");
        return NULL;
    }

    memcpy(buf, data, len);

    buf[len] = '\0';

    data_parser_ctx_t ctx = {
        .root = cJSON_CreateObject(),
        .current_cmd = NULL,
    };

    if (!ctx.root)
    {
        return NULL;
    }

    char *saveptr = NULL;

    // first token = device token
    char *token = strtok_r(buf, ":", &saveptr);

    if (!token)
    {
        cJSON_Delete(ctx.root);
        return NULL;
    }

    // device token
    cJSON_AddStringToObject(ctx.root, "token", token);

    char *current = NULL;

    // iterate entire string until end
    while ((current = strtok_r(NULL, ":", &saveptr)))
    {
        trim_trailing_whitespace(current);
        char *cmd_eq = strchr(current, '=');
        if (cmd_eq)
        {
            size_t lhs_len = cmd_eq - current;
            char lhs[64];
            if (lhs_len >= sizeof(lhs))
            {
                ESP_LOGW("PARSER", "Command token too large");
                continue;
            }
            memcpy(lhs, current, lhs_len);
            lhs[lhs_len] = '\0';
            char *rhs = cmd_eq + 1;
            trim_trailing_whitespace(rhs);

            const cmd_desc_t *cmd = find_cmd(lhs);

            if (cmd)
            {
                // COMMAND=?
                if (strcmp(rhs, "?") == 0)
                {
                    register_help(&ctx, cmd->name);

                    ctx.current_cmd = cmd;

                    continue;
                }
                else // COMMAND=FIELD
                {
                    const field_desc_t *field = find_field(cmd, rhs);
                    if (field)
                    {
                        cJSON *obj = cJSON_GetObjectItem(ctx.root, cmd->json_obj_name);
                        if (!obj)
                        {
                            obj = cJSON_CreateObject();
                            cJSON_AddItemToObject(ctx.root, cmd->json_obj_name, obj);
                        }
                        // HELP override: speacila case
                        if (strcasecmp(field->name, "HELP") == 0)
                        {
                            register_all_help(&ctx);
                        }
                        else
                        {
                            cJSON_AddNumberToObject(obj, field->json_key, 1);
                        }
                        continue;
                    }
                }
            }
        }

        const cmd_desc_t *cmd = find_cmd(current);
        if (cmd) // is its command
        {
            ctx.current_cmd = cmd; // activate command context
            continue;
        }
        else // not a command, it's a filed or "?"
        {
            // must have active command
            if (!ctx.current_cmd)
            {
                ESP_LOGW("PARSER", "Field without command context [%s]", current);
                continue;
            }
            // check if current is field operation
            // FIELD=value
            // FIELD=?
            // FIELD=GET
            char *eq = strchr(current, '=');
            if (!eq)
            {
                ESP_LOGW("PARSER", "Invalid current token [%s]", current);

                continue;
            }
            else
            {
                *eq = '\0';
                // split lhs/rhs
                char *lhs = current;
                char *rhs = eq + 1;
                trim_trailing_whitespace(rhs);
                // check if lhs is valid field
                const field_desc_t *field = find_field(ctx.current_cmd, lhs);
                if (field)
                {

                    // check if rhs is '?': help request
                    if (strcmp(rhs, "?") == 0)
                    {
                        register_help(&ctx, ctx.current_cmd->name);
                    }
                    else if (strcasecmp(rhs, PARSER_GET_KEYWORD) == 0) //// request current field value
                    {
                        if (!(field->ops & FIELD_OP_GET))
                        {
                            ESP_LOGW("PARSER", "GET not supported for [%s]", field->name);
                        }
                        else
                        {
                            cJSON *obj = cJSON_GetObjectItem(ctx.root, ctx.current_cmd->json_obj_name);

                            if (!obj)
                            {
                                obj = cJSON_CreateObject();
                                cJSON_AddItemToObject(ctx.root, ctx.current_cmd->json_obj_name, obj);
                            }

                            register_get_request(obj, field->json_key);
                        }
                    }
                    else // FIELD=value
                    {
                        if (!(field->ops & FIELD_OP_SET))
                        {
                            ESP_LOGW("PARSER", "SET not supported for [%s]", field->name);
                        }
                        else
                        {
                            cJSON *obj = cJSON_GetObjectItem(ctx.root, ctx.current_cmd->json_obj_name);

                            if (!obj)
                            {
                                obj = cJSON_CreateObject();
                                cJSON_AddItemToObject(ctx.root, ctx.current_cmd->json_obj_name, obj);
                            }
                            apply_fields(obj, field, rhs);
                        }
                    }
                    continue;
                }
                else
                {
                    ESP_LOGW("PARSER", "Invalid field [%s]", lhs);

                    continue;
                }
            }
        }
    }
    return ctx.root;
}

static const field_desc_t *find_field_by_json_key(const cmd_desc_t *cmd, const char *json_key)
{
    if (!cmd || !json_key)
    {
        return NULL;
    }

    for (size_t i = 0; i < cmd->field_count; i++)
    {
        if (strcasecmp(cmd->fields[i].json_key, json_key) == 0)
        {
            return &cmd->fields[i];
        }
    }

    return NULL;
}

char *json_data_parse_to_serial(const cJSON *root)
{
    if (!root)
    {
        return NULL;
    }

    // output buffer
    char *out = calloc(1, 512);

    if (!out)
    {
        return NULL;
    }

    // TOKEN
    cJSON *token = cJSON_GetObjectItem(root, "token");

    if (!cJSON_IsString(token) || !token->valuestring)
    {
        free(out);
        return NULL;
    }

    strcat(out, token->valuestring);


    // iterate all commands
    for (size_t i = 0; i < g_cmd_count; i++)
    {
        const cmd_desc_t *cmd = g_cmds[i];

        cJSON *obj =
            cJSON_GetObjectItem(root, cmd->json_obj_name);

        if (!obj || !cJSON_IsObject(obj))
        {
            continue;
        }

        
        // SPLCMD special handling
        if (cmd == &spl_cmds)
        {
            cJSON *item = NULL;

            cJSON_ArrayForEach(item, obj)
            {
                //
                // HELP array
                //
                if (strcasecmp(item->string, "help") == 0)
                {
                    if (!cJSON_IsArray(item))
                    {
                        continue;
                    }

                    
                    // HELP=["ALL"]
                    cJSON *help_it = cJSON_GetArrayItem(item, 0);

                    if (help_it && cJSON_IsString(help_it) && strcasecmp(help_it->valuestring, "ALL") == 0)
                    {
                        strcat(out, ":SPLCMD=HELP");

                        continue;
                    }

                    
                    // HELP=["LED","RGB"]
                    cJSON *help = NULL;

                    cJSON_ArrayForEach(help, item)
                    {
                        if (!cJSON_IsString(help))
                        {
                            continue;
                        }

                        strcat(out, ":");
                        strcat(out, help->valuestring);
                        strcat(out, "=?");
                    }

                    continue;
                }

                
                // SPLCMD actions
                if (cJSON_IsNumber(item) && item->valuedouble == 1)
                {
                    const field_desc_t *field = find_field_by_json_key(cmd, item->string);

                    if (!field)
                    {
                        continue;
                    }

                    strcat(out, ":");
                    strcat(out, cmd->name);
                    strcat(out, "=");
                    strcat(out, field->name);
                }
            }

            continue;
        }

        // normal command object
        strcat(out, ":");
        strcat(out, cmd->name);

        cJSON *item = NULL;

        cJSON_ArrayForEach(item, obj)
        {
            
            // GET array
            if (strcasecmp(item->string, "get") == 0)
            {
                if (!cJSON_IsArray(item))
                {
                    continue;
                }

                cJSON *get = NULL;

                cJSON_ArrayForEach(get, item)
                {
                    if (!cJSON_IsString(get))
                    {
                        continue;
                    }

                    const field_desc_t *field = find_field_by_json_key(cmd, get->valuestring);

                    if (!field)
                    {
                        continue;
                    }

                    strcat(out, ":");
                    strcat(out, field->name);
                    strcat(out, "=GET");
                }

                continue;
            }


            // normal fields
            const field_desc_t *field = find_field_by_json_key(cmd, item->string);

            if (!field)
            {
                continue;
            }

            strcat(out, ":");
            strcat(out, field->name);
            strcat(out, "=");

            // INT / BOOL
            if (cJSON_IsNumber(item))
            {
                char num_buf[32];

                snprintf(num_buf, sizeof(num_buf), "%d", item->valueint);

                strcat(out, num_buf);
            }
            // STRING
            else if (cJSON_IsString(item))
            {
                strcat(out, item->valuestring);
            }
        }
    }

    return out;
}