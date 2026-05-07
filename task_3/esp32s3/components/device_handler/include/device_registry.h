#ifndef DEVICE_REGISTRY_H
#define DEVICE_REGISTRY_H


#include <stdint.h>

#define MAX_DEVICES 5

typedef struct {
    char token[32];

    uint32_t seq_tx;
    uint32_t seq_ack;

    uint8_t active;
    uint8_t authenticated;

    uint64_t last_tx_time;

    uint64_t last_ack_time_us;
    
    uint32_t mirror_blink_ms;

} device_entry_t;

void init_device_registry(void);

device_entry_t* device_find_by_token(const char *token);
device_entry_t* device_alloc_by_token(const char *token);

void device_mark_authenticated(const char *token);

void device_update_tx(device_entry_t *dev);
void device_update_ack_and_time(device_entry_t *dev, uint32_t seq);


#endif // DEVICE_REGISTRY_H