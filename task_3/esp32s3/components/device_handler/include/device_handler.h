#ifndef DEVICE_HANDLER_H
#define DEVICE_HANDLER_H

#include "device_registry.h"

void init_device_handler(void);
void device_connect(const char *token, uint8_t bypass);
void device_send_command(const char *cmd);



#endif // DEVICE_HANDLER_H