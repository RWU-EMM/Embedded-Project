#include "auth.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/time.h"

// Adjust as needed for your application
#define AUTH_TIMEOUT_MS 20000
#define DEVICE_SECRET "PICO_SECRET"
#define MAX_AUTH_SESSIONS 4

typedef struct
{
    char in_key[32];
    char out_key[32];
    absolute_time_t start_time;
    bool active;
} auth_session_t;

static auth_session_t sessions[MAX_AUTH_SESSIONS];

static char tokens[MAX_TOKENS][32];
static int token_count = 0;

// Note: This is NOT cryptographically secure. For demo purposes only.
static uint32_t simple_hash(const char *str)
{
    uint32_t hash = 5381;
    int c;

    while ((c = *str++))
        hash = ((hash << 5) + hash) ^ c;

    return hash;
}
// Generate expected auth value based on in_key, out_key, and a secret
static uint32_t generate_auth(const char *in, const char *out)
{
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%s%s%s", in, out, DEVICE_SECRET);
    return simple_hash(buffer);
}

// RANDOM STRING
// Note: This is NOT cryptographically secure. For demo purposes only.
static void generate_random_string(char *buf, int len)
{
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    for (int i = 0; i < len - 1; i++)
    {
        buf[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    buf[len - 1] = '\0';
}

static void generate_random_key(char *buf, int len)
{
    uint32_t r = rand(); // or better: use hardware RNG if available

    // convert to HEX string
    snprintf(buf, len, "%08X", r); // 8 hex chars
}

static auth_session_t *alloc_session(void)
{
    for (int i = 0; i < MAX_AUTH_SESSIONS; i++)
    {
        if (!sessions[i].active)
            return &sessions[i];
    }
    return NULL;
}

static auth_session_t *find_session_by_out_key(const char *out_key)
{
    for (int i = 0; i < MAX_AUTH_SESSIONS; i++)
    {
        if (sessions[i].active &&
            strcmp(sessions[i].out_key, out_key) == 0)
        {
            return &sessions[i];
        }
    }
    return NULL;
}

// Clears all auth state and tokens
void auth_init(void)
{
    memset(tokens, 0, sizeof(tokens));
    token_count = 0;

    for (int i = 0; i < MAX_AUTH_SESSIONS; i++)
    {
        sessions[i].active = false;
    }
}

// STEP 1: START AUTH
// Returns true if in_key is valid and out_key is generated, false otherwise.
bool auth_start(const char *in_key, char *out_key)
{
    if (!in_key || !out_key)
        return false;

    auth_session_t *s = alloc_session();
    if (!s)
    {
        printf("No free auth sessions\n");
        return false;
    }

    strncpy(s->in_key, in_key, sizeof(s->in_key) - 1);
    s->in_key[31] = '\0';

    generate_random_key(s->out_key, sizeof(s->out_key));

    strcpy(out_key, s->out_key);

    s->start_time = get_absolute_time();
    s->active = true;

    printf("AUTH started (session)\n");

    return true;
}

// STEP 2: VERIFY
// Returns true if reg matches expected value and token is registered, false otherwise.
bool auth_verify_and_register(const char *out_key,
                              const char *reg,
                              const char *token)
{
    auth_session_t *s = find_session_by_out_key(out_key);
    if (!s)
    {
        printf("Session not found\n");
        return false;
    }

    uint32_t expected = generate_auth(s->in_key, s->out_key);
    uint32_t received = (uint32_t)strtoul(reg, NULL, 10);

    if (expected == received)
    {
        absolute_time_t now = get_absolute_time();

        if (absolute_time_diff_us(s->start_time, now) > AUTH_TIMEOUT_MS * 1000)
        {
            printf("Session expired\n");
            s->active = false;
            return false;
        }

        // @todo: verify for re-registration based on token, to prevent multiple tokens for same session
        if (token_count < MAX_TOKENS)
        {
            strncpy(tokens[token_count], token, 31);
            tokens[token_count][31] = '\0';
            token_count++;

            printf("Token registered: %s\n", token);

            s->active = false; // close session
            return true;
        }
    }

    printf("AUTH failed\n");
    s->active = false;
    return false;
}

// TOKEN CHECK
// Returns true if token is valid, false otherwise.
bool auth_is_token_valid(const char *token)
{
    for (int i = 0; i < token_count; i++)
    {
        if (strcmp(tokens[i], token) == 0)
            return true;
    }
    return false;
}

// TIMEOUT HANDLER
// Call periodically to check for auth timeout
void auth_process(void)
{
    absolute_time_t now = get_absolute_time();

    for (int i = 0; i < MAX_AUTH_SESSIONS; i++)
    {
        if (sessions[i].active)
        {
            if (absolute_time_diff_us(sessions[i].start_time, now) > AUTH_TIMEOUT_MS * 1000)
            {
                printf("AUTH timeout (session)\n");
                sessions[i].active = false;
            }
        }
    }
}

// Reset auth state (e.g., on timeout)
void auth_reset(void)
{
    for (int i = 0; i < MAX_AUTH_SESSIONS; i++)
    {
        sessions[i].active = false;
    }
}