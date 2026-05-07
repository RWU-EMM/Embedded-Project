#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H


#include <stdbool.h>
#include "lwip/apps/mqtt.h"
#include "lwip/ip_addr.h"
#include "pico/cyw43_arch.h"


#ifndef TOPIC_CONNECT
#define TOPIC_CONNECT "/connect"
#endif

#ifndef TOPIC_CMD
#define TOPIC_CMD "/cmd"
#endif

#ifndef TOPIC_ACK
#define TOPIC_ACK "/ack"
#endif

#ifndef TOPIC_DATA
#define TOPIC_DATA "/data"
#endif


// Config structure for MQTT client
typedef void (*mqtt_data_cb_t)(const char *topic, const char *data);

typedef struct
{
    const char *client_id;
    const char *username;
    const char *password;
    const char *server;
    uint16_t port;
    uint16_t keep_alive;

    mqtt_data_cb_t data_cb;   // user callback

} mqtt_client_config_t;


// API's
bool init_mqtt_client(mqtt_client_config_t *cfg);
bool reinit_mqtt_client(void);
void deinit_mqtt_client(void);

bool mqtt_client_sub_topic(const char *topic);
bool mqtt_client_unsub_topic(const char *topic);

bool mqtt_client_pub(const char *topic, const char *msg);
bool mqtt_client_unpub(const char *topic); // clear retained

// polling (must be called in main loop)
void mqtt_client_poll(void);

// async worker registration
void mqtt_register_periodic_worker(async_at_time_worker_t *worker, uint32_t interval_ms);

bool mqtt_is_connected(void);

bool mqtt_is_connecting(void);

#endif // MQTT_CLIENT_H