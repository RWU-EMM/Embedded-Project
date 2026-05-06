#ifndef AUTH_CLIENT_H
#define AUTH_CLIENT_H

#include <stdint.h>

typedef enum {
    AUTH_IDLE,
    AUTH_WAIT_OUT_KEY,
    AUTH_WAIT_VERIFY_ACK,
    AUTH_DONE
} auth_state_t;

void init_auth(void);
void auth_start(const char *token, uint8_t bypass);
void auth_handle_response(const char *topic, const char *payload);

uint8_t auth_is_done(void);
const char* auth_get_token(void);


#endif // AUTH_CLIENT_H