#ifndef AUTH_H
#define AUTH_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_TOKENS 5

// Init
void auth_init(void);

// Step 1: handle {"in_key": "..."}
bool auth_start(const char *in_key, char *out_key);

// Step 2: handle {"reg":"...", "token":"..."}
bool auth_verify_and_register(const char *out_key, const char *reg, const char *token);

// Check token validity
bool auth_is_token_valid(const char *token);

// Reset auth state (timeout)
void auth_reset(void);

// Call periodically for timeout handling
void auth_process(void);

bool direct_token_registration(const char *out_key, const char *token);

#endif // AUTH_H