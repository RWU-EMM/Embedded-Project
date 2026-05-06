#include "mqtt_client.h"

#include <stdio.h>
#include <string.h>

#include "lwip/dns.h"

// INTERNAL STATE

typedef struct
{
    mqtt_client_t *client;
    struct mqtt_connect_client_info_t info;
    const char *server_name;
    ip_addr_t server_ip;
    uint16_t port;

    bool connected;
    bool connecting;
    bool initialized;

    mqtt_data_cb_t user_cb;

    char rx_buffer[256];
    char topic_buffer[128];

} mqtt_client_ctx_t;

static mqtt_client_ctx_t ctx;

static uint16_t rx_len = 0;

// MQTT publish callback
static void pub_cb(void *arg, err_t err)
{
    if (err != ERR_OK)
        printf("MQTT pub error: %d\n", err);
}

// Called when a new message is published to a subscribed topic
static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
    strncpy(ctx.topic_buffer, topic, sizeof(ctx.topic_buffer));
}

// Called when data is received for a subscribed topic
static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    if (rx_len + len >= sizeof(ctx.rx_buffer))
    {
        printf("MQTT RX overflow\n");
        rx_len = 0;
        return;
    }

    memcpy(&ctx.rx_buffer[rx_len], data, len);
    rx_len += len;

    // If this is the LAST chunk
    if (flags & MQTT_DATA_FLAG_LAST)
    {
        ctx.rx_buffer[rx_len] = '\0';

        if (ctx.user_cb)
        {
            ctx.user_cb(ctx.topic_buffer, ctx.rx_buffer);
        }

        rx_len = 0;  // reset buffer
    }
}

// MQTT connection status callback
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
    ctx.connecting = false;
    if (status == MQTT_CONNECT_ACCEPTED)
    {
        ctx.connected = true;
        printf("MQTT Connected\n");
    }
    else
    {
        printf("MQTT Connection failed: %d\n", status);
        ctx.connected = false;
        ctx.client = NULL;
    }
}

// DNS callback when resolving server hostname
static void dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    if (!ipaddr)
    {
        printf("DNS failed\n");
        return;
    }

    ctx.server_ip = *ipaddr;

    ctx.client = mqtt_client_new();
    if (!ctx.client)
    {
        printf("MQTT client alloc failed\n");
        return;
    }

    ctx.connecting = true;

    cyw43_arch_lwip_begin();

    mqtt_client_connect(
        ctx.client,
        &ctx.server_ip,
        ctx.port,
        mqtt_connection_cb,
        &ctx,
        &ctx.info);

    mqtt_set_inpub_callback(
        ctx.client,
        mqtt_incoming_publish_cb,
        mqtt_incoming_data_cb,
        &ctx);

    cyw43_arch_lwip_end();
}

// Initialize MQTT client with configuration
bool init_mqtt_client(mqtt_client_config_t *cfg)
{
    memset(&ctx, 0, sizeof(ctx));

    ctx.user_cb = cfg->data_cb;

    ctx.info.client_id = cfg->client_id;
    ctx.info.client_user = cfg->username;
    ctx.info.client_pass = cfg->password;
    ctx.info.keep_alive = cfg->keep_alive;
    ctx.port = cfg->port;

    ctx.initialized = true;

    ctx.server_name = cfg->server;

    cyw43_arch_lwip_begin();
    int err = dns_gethostbyname(cfg->server, &ctx.server_ip, dns_found_cb, NULL);
    cyw43_arch_lwip_end();

    if (err == ERR_OK)
    {
        dns_found_cb(cfg->server, &ctx.server_ip, NULL);
    }

    return true;
}

// Reinitialize MQTT client (disconnect and reconnect)
bool reinit_mqtt_client(void)
{

    if (ctx.connecting) return false;

    deinit_mqtt_client();

    if (!ctx.initialized)
        return false;


    cyw43_arch_lwip_begin();
    int err = dns_gethostbyname(ctx.server_name, &ctx.server_ip, dns_found_cb, &ctx);
    cyw43_arch_lwip_end();

    if (err == ERR_OK)
    {
        dns_found_cb(ctx.server_name, &ctx.server_ip, NULL);
    }

    return (err == ERR_OK || err == ERR_INPROGRESS);
}

// Disconnect and clean up MQTT client
void deinit_mqtt_client(void)
{
    if (ctx.client)
    {
        mqtt_disconnect(ctx.client);
        ctx.client = NULL;
    }

    ctx.connected = false;
}

// Publish a message to a topic
bool  mqtt_client_pub(const char *topic, const char *msg)
{
    if (!ctx.client || !mqtt_client_is_connected(ctx.client))
        return false;

    cyw43_arch_lwip_begin();

    mqtt_publish(ctx.client, topic, msg, strlen(msg), 0, 0, pub_cb, NULL);

    cyw43_arch_lwip_end();

    return true;
}

// Clear retained message by publishing empty payload with retain flag
bool mqtt_client_unpub(const char *topic)
{
    if (!ctx.client || !mqtt_client_is_connected(ctx.client))
        return false;

    mqtt_publish(ctx.client, topic, "", 0, 1, 1, pub_cb, NULL);
    return true;
}
// Subscribe to a topic
bool mqtt_client_sub_topic(const char *topic)
{
    if (!ctx.client || !mqtt_client_is_connected(ctx.client))
        return false;

    mqtt_subscribe(ctx.client, topic, 1, NULL, NULL);
    return true;
}

// Unsubscribe from a topic
bool mqtt_client_unsub_topic(const char *topic)
{
    if (!ctx.client || !mqtt_client_is_connected(ctx.client))
        return false;

    mqtt_unsubscribe(ctx.client, topic, NULL, NULL);
    return true;
}

// POLLING (must be called in main loop)
void mqtt_client_poll(void)
{
    cyw43_arch_poll();
}

// async worker registration
void mqtt_register_periodic_worker(async_at_time_worker_t *worker, uint32_t interval_ms)
{
    async_context_add_at_time_worker_in_ms(
        cyw43_arch_async_context(),
        worker,
        interval_ms);
}

bool mqtt_is_connected(void)
{
    return (ctx.client && mqtt_client_is_connected(ctx.client));
}

bool mqtt_is_connecting(void)
{
    return ctx.connecting;
}