#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─────────────────────────────────────────────
 * Limits
 * ───────────────────────────────────────────── */
#define MQTT_MAX_SUBSCRIPTIONS      16
#define MQTT_MAX_TOPIC_LEN          96
#define MQTT_MAX_HANDLERS           16      /* topic-filter → callback table size */

/* ─────────────────────────────────────────────
 * Transport / state enums  (unchanged from v1)
 * ───────────────────────────────────────────── */
typedef enum {
    MQTT_TRANSPORT_TCP = 0,
    MQTT_TRANSPORT_TLS,
    MQTT_TRANSPORT_WS,
    MQTT_TRANSPORT_WSS
} mqtt_transport_t;

typedef enum {
    MQTT_STATE_STOPPED = 0,
    MQTT_STATE_STARTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_DISCONNECTED,
    MQTT_STATE_ERROR
} mqtt_state_t;

/* ─────────────────────────────────────────────
 * Callback type
 *
 * topic    – null-terminated topic string
 * data     – raw payload bytes (NOT null-terminated)
 * data_len – payload length in bytes
 *
 * The callback runs inside the MQTT task context.
 * Keep it short: copy data, post to a queue, set a flag.
 * Do NOT call mqtt_service_publish() from inside a callback
 * (deadlock risk on the internal mutex).
 * ───────────────────────────────────────────── */
typedef void (*mqtt_rx_cb_t)(const char *topic,
                             const char *data,
                             int         data_len);

/* ─────────────────────────────────────────────
 * Configuration
 *
 * All string fields are deep-copied on mqtt_service_init()
 * and mqtt_service_reconfigure().  You do NOT need to keep
 * the strings alive after the call returns.
 * ───────────────────────────────────────────── */
typedef struct {
    mqtt_transport_t  transport;
    const char       *host;
    uint16_t          port;
    const char       *path;          /* WS/WSS only, e.g. "/mqtt"    */
    const char       *client_id;
    const char       *username;      /* NULL = no auth                */
    const char       *password;      /* NULL = no auth                */
    const char       *ca_cert;       /* PEM string, required TLS/WSS  */
    int               keepalive_sec;
    bool              clean_session;
    bool              auto_reconnect;
} mqtt_service_config_t;

/* ─────────────────────────────────────────────
 * Lifecycle
 * ───────────────────────────────────────────── */

/** Initialise service. Deep-copies all strings from cfg. */
esp_err_t mqtt_service_init(const mqtt_service_config_t *cfg);

/** Start the MQTT client and begin connecting. */
esp_err_t mqtt_service_start(void);

/** Gracefully stop the MQTT client (keeps config + subscriptions). */
esp_err_t mqtt_service_stop(void);

/** Stop, free everything, return to uninitialized state. */
esp_err_t mqtt_service_deinit(void);

/**
 * Switch transport (or any other setting) at runtime.
 * Deep-copies new cfg, tears down the current client,
 * rebuilds and restarts. Registered callbacks and the
 * subscription table are preserved.
 */
esp_err_t mqtt_service_reconfigure(const mqtt_service_config_t *cfg);

/* ─────────────────────────────────────────────
 * Pub / Sub
 * ───────────────────────────────────────────── */
esp_err_t mqtt_service_publish(const char *topic,
                               const char *payload,
                               int         qos,
                               int         retain);

esp_err_t mqtt_service_subscribe(const char *topic, int qos);
esp_err_t mqtt_service_unsubscribe(const char *topic);

/* ─────────────────────────────────────────────
 * Message routing
 *
 * Register a callback that fires when a message arrives
 * on a topic that matches `filter`.
 *
 * Filter rules (MQTT wildcard subset):
 *   "devices/node01/cmd"   – exact match
 *   "devices/+/cmd"        – single-level wildcard
 *   "devices/#"            – multi-level wildcard (must be last segment)
 *
 * Multiple callbacks may match one message — all are called
 * in registration order.
 *
 * mqtt_service_register_handler()   – add a filter→callback pair
 * mqtt_service_unregister_handler() – remove by filter string
 * mqtt_service_set_default_handler() – catch-all for unmatched messages
 * ───────────────────────────────────────────── */
esp_err_t mqtt_service_register_handler(const char *filter, mqtt_rx_cb_t cb);
esp_err_t mqtt_service_unregister_handler(const char *filter);
void      mqtt_service_set_default_handler(mqtt_rx_cb_t cb);

/* ─────────────────────────────────────────────
 * Status
 * ───────────────────────────────────────────── */
bool         mqtt_service_is_connected(void);
mqtt_state_t mqtt_service_get_state(void);

#ifdef __cplusplus
}
#endif
