#ifndef WIFI_UDP_HANDLER_H
#define WIFI_UDP_HANDLER_H

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

typedef struct
{
    const char *target_ip;
    uint16_t target_port;

} wifi_udp_handler_config_t;

esp_err_t wifi_udp_handler_init(const wifi_udp_handler_config_t *config);

esp_err_t wifi_udp_handler_send(const void *data,size_t length);

#endif // WIFI_UDP_HANDLER_H