#ifndef WIFI_DRIVER_H
#define WIFI_DRIVER_H 

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "esp_netif_net_stack.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include <cJSON.h>

#define MAX_CONNECTION_RETRY 5
#define CONFIG_AP_WIFI_CHANNEL 8
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

extern int wifi_connected;
extern char g_ap_ip_str[16];
extern uint8_t g_got_wifi_data_flag;
extern uint8_t g_device_connected_flag;
extern httpd_handle_t g_webserver_handler;
void esp_start_wifi_driver(int wifi_op_mode, const char *ap_ssid, const char *ap_passwd, const char *sta_ssid, const char *sta_passwd);

void stop_wifi_driver(void);
httpd_handle_t start_webserver(void);
void stop_webserver(httpd_handle_t server);
esp_err_t nvs_initialize(void);
esp_err_t nvs_save_wifi_credentials(const char *ssid, const char *password);
esp_err_t nvs_read_wifi_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size);
esp_err_t nvs_erase_wifi_credentials(void);

#endif /* WIFI_DRIVER_H */