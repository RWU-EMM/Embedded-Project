#include "wifi_driver.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include <string.h>

#ifndef WIFI_SSID
#error "WIFI_SSID not defined"
#endif

#ifndef WIFI_AUTH
#error "WIFI_AUTH not defined"
#endif

#define WIFI_CONNECT_TIMEOUT_MS 30000

static uint8_t wifi_connected = false;

uint8_t init_wifi_sta(void)
{
    if (cyw43_arch_init())
    {
        printf("WiFi init failed\n");
        return 0;
    }

    cyw43_arch_enable_sta_mode();
    return 1;
}

uint8_t wifi_connect(char *ssid, char *pass)
{
    const char *use_ssid;
    const char *use_pass;
    uint32_t auth;

    // ---------------------------
    // Handle SSID fallback
    // ---------------------------
    if (ssid == NULL)
    {
        use_ssid = WIFI_SSID;
    }
    else
    {
        use_ssid = ssid;
    }

    // ---------------------------
    // Handle password + auth
    // ---------------------------
    if (pass == NULL)
    {
        if(strlen(WIFI_PASSWORD) == 0){
            use_pass = "";
            auth = CYW43_AUTH_OPEN;
        }
        else{
            use_pass = WIFI_PASSWORD;
        }   
    }
    else
    {
        use_pass = pass;
        auth = CYW43_AUTH_WPA2_AES_PSK;
    }

    printf("Connecting to WiFi: %s\n", use_ssid);

    int err = cyw43_arch_wifi_connect_timeout_ms(
        use_ssid,
        use_pass,
        auth,
        WIFI_CONNECT_TIMEOUT_MS
    );

    if (0 != err)
    {
        printf("WiFi connection failed (%d)\n", err);
        wifi_connected = 0;
        return 0;
    }

    printf("WiFi connected\n");
    wifi_connected = 1;
    return 1;
}

void wifi_disconnect(void)
{
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    wifi_connected = 0;
}


uint8_t wifi_is_connected(void)
{
    return wifi_connected;
}


void wifi_poll(void)
{
    cyw43_arch_poll();
}