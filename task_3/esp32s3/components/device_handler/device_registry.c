#include "device_handler.h"

#include<stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_timer.h"

#include "esp_log.h"

static device_entry_t devices[MAX_DEVICES];

void init_device_registry(void)
{
    memset(devices, 0, sizeof(devices));
}

device_entry_t* device_find_by_token(const char *token)
{
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].active &&
            strcmp(devices[i].token, token) == 0)
            return &devices[i];
    }
    return NULL;
}

device_entry_t* device_alloc_by_token(const char *token)
{
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!devices[i].active) {
            memset(&devices[i], 0, sizeof(device_entry_t));
            strcpy(devices[i].token, token);
            devices[i].active = 1;
            return &devices[i];
        }
    }
    return NULL;
}

void device_mark_authenticated(const char *token)
{
    device_entry_t *dev = device_find_by_token(token);
    if (!dev)
        dev = device_alloc_by_token(token);

    if (dev)
        dev->authenticated = 1;
}

void device_update_tx(device_entry_t *dev)
{
    dev->seq_tx++;
    dev->last_tx_time = esp_timer_get_time();
}

void device_update_ack(device_entry_t *dev, uint32_t seq)
{
    dev->seq_ack = seq;
}
