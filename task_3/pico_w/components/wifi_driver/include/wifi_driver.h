#ifndef WIFI_DRIVER_H
#define WIFI_DRIVER_H

#include <stdint.h>

// Init WiFi stack (CYW43)
uint8_t init_wifi_sta(void);

// Connect to AP (blocking with timeout)
uint8_t wifi_connect(char *ssid, char *pass);

// Disconnect WiFi
void wifi_disconnect(void);

// Check connection status
uint8_t wifi_is_connected(void);

// Poll (must be called in main loop)
void wifi_poll(void);



#endif // WIFI_DRIVER_H