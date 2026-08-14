#include "wifi_udp_handler.h"
#include "wifi_driver.h"

#include <stdio.h>

#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "esp_log.h"
#include "esp_check.h"
#include <errno.h>

typedef struct
{
    int sock;

    struct sockaddr_in dest_addr;

    uint8_t initialized;

} wifi_udp_context_t;

static wifi_udp_context_t s_ctx;

esp_err_t wifi_udp_handler_init(const wifi_udp_handler_config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_initialize();

    esp_start_wifi_driver(1, "ESP_AP", "12144121", "Luc Yonga", "Lucyonga@0905");
    // esp_start_wifi_driver(1, "ESP_AP", "12144121", "H.O.M.E", "HOME2077");
    // esp_start_wifi_driver(1, "ESP_AP", "12141214", "NoDevice", "13071307");

    memset(&s_ctx, 0, sizeof(s_ctx));

    s_ctx.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    if (s_ctx.sock < 0)
    {
        ESP_LOGE(__func__, "socket create failed");
        return ESP_FAIL;
    }

    s_ctx.dest_addr.sin_family = AF_INET;

    s_ctx.dest_addr.sin_port = htons(config->target_port);

    s_ctx.dest_addr.sin_addr.s_addr = inet_addr(config->target_ip);

    s_ctx.initialized = 1;

    return ESP_OK;
}

esp_err_t wifi_udp_handler_send(
    const void *data,
    size_t length)
{
    if ((data == NULL) || (length == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

    int ret;

    ret = sendto(s_ctx.sock, data, length, 0, (struct sockaddr *)&s_ctx.dest_addr, sizeof(s_ctx.dest_addr));

    if (ret < 0)
    {
        ESP_LOGE("UDP", "sendto failed errno=%d", errno);
        return ESP_FAIL;
    }

    // ESP_LOGI("HEAP", "free=%d", (int)esp_get_free_heap_size());

    if ((size_t)ret != length)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}