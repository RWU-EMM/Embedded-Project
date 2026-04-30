#include "mqtt_service.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"


/* ─────────────────────────────────────────────
 * Internal config — owns its own string copies
 * Every pointer here was allocated with strdup()
 * (or is NULL).  free_config_strings() releases them.
 * ───────────────────────────────────────────── */
typedef struct {
    mqtt_transport_t  transport;
    char             *host;
    uint16_t          port;
    char             *path;
    char             *client_id;
    char             *username;
    char             *password;
    char             *ca_cert;       /* full PEM string, heap-allocated */
    int               keepalive_sec;
    bool              clean_session;
    bool              auto_reconnect;
} mqtt_cfg_owned_t;

/* ─────────────────────────────────────────────
 * Subscription table entry
 * ───────────────────────────────────────────── */
typedef struct {
    char topic[MQTT_MAX_TOPIC_LEN];
    int  qos;
    bool in_use;
} mqtt_sub_entry_t;

/* ─────────────────────────────────────────────
 * Handler table entry  (filter → callback)
 * ───────────────────────────────────────────── */
typedef struct {
    char         filter[MQTT_MAX_TOPIC_LEN];
    mqtt_rx_cb_t cb;
    bool         in_use;
} mqtt_handler_entry_t;

/* ─────────────────────────────────────────────
 * Module context  (single static instance)
 * ───────────────────────────────────────────── */
#define MQTT_MAX_URI_LEN  192

typedef struct {
    esp_mqtt_client_handle_t client;
    mqtt_cfg_owned_t         cfg;
    mqtt_state_t             state;
    bool                     initialized;
    bool                     started;
    SemaphoreHandle_t        lock;
    mqtt_sub_entry_t         subs[MQTT_MAX_SUBSCRIPTIONS];
    mqtt_handler_entry_t     handlers[MQTT_MAX_HANDLERS];
    mqtt_rx_cb_t             default_handler;
    char                     uri[MQTT_MAX_URI_LEN];
} mqtt_ctx_t;

static mqtt_ctx_t s_mqtt = {0};

/* ═══════════════════════════════════════════════
 * Config string memory management
 * ═══════════════════════════════════════════════ */

/* strdup that returns NULL (not garbage) when src is NULL */
static char *dup_or_null(const char *src)
{
    return src ? strdup(src) : NULL;
}

static void free_config_strings(mqtt_cfg_owned_t *cfg)
{
    free(cfg->host);      cfg->host      = NULL;
    free(cfg->path);      cfg->path      = NULL;
    free(cfg->client_id); cfg->client_id = NULL;
    free(cfg->username);  cfg->username  = NULL;
    free(cfg->password);  cfg->password  = NULL;
    free(cfg->ca_cert);   cfg->ca_cert   = NULL;
}

/**
 * Deep-copy all strings from a caller-supplied mqtt_service_config_t
 * into our owned internal cfg.  Frees any existing strings first.
 */
static esp_err_t copy_config(mqtt_cfg_owned_t *dst, const mqtt_service_config_t *src)
{
    free_config_strings(dst);   /* release previous copies if reconfiguring */

    dst->transport     = src->transport;
    dst->port          = src->port;
    dst->keepalive_sec = src->keepalive_sec;
    dst->clean_session = src->clean_session;
    dst->auto_reconnect= src->auto_reconnect;

    dst->host      = dup_or_null(src->host);
    dst->path      = dup_or_null(src->path);
    dst->client_id = dup_or_null(src->client_id);
    dst->username  = dup_or_null(src->username);
    dst->password  = dup_or_null(src->password);
    dst->ca_cert   = dup_or_null(src->ca_cert);

    /* host is the only truly mandatory string */
    ESP_RETURN_ON_FALSE(dst->host != NULL, ESP_ERR_NO_MEM, __func__, "strdup host failed");

    return ESP_OK;
}

/* ═══════════════════════════════════════════════
 * Topic filter matching
 *
 * Supports:
 *   exact match          "a/b/c"  → "a/b/c"
 *   single-level  '+'   "a/+/c"  → "a/foo/c"
 *   multi-level   '#'   "a/#"    → "a/b/c/d"
 * ═══════════════════════════════════════════════ */
static bool topic_matches(const char *filter, const char *topic)
{
    const char *f = filter;
    const char *t = topic;

    while (*f && *t) {
        if (*f == '#') {
            /* '#' matches everything that remains (including '/') */
            return true;
        }
        if (*f == '+') {
            /* '+' matches one segment — advance t to next '/' or end */
            while (*t && *t != '/') t++;
            f++;  /* skip '+' */
            /* both should now be at '/' or end */
        } else if (*f == *t) {
            f++;
            t++;
        } else {
            return false;
        }
    }

    /* consume a trailing '#' in the filter (matches empty remainder) */
    if (*f == '#') return true;

    return (*f == '\0' && *t == '\0');
}

/* ═══════════════════════════════════════════════
 * Handler table operations  (call with lock held)
 * ═══════════════════════════════════════════════ */
static void dispatch_message_locked(const char *topic, int topic_len,
                                    const char *data,  int data_len)
{
    /* Make a null-terminated copy of the topic for matching */
    char topic_buf[MQTT_MAX_TOPIC_LEN];
    int  copy_len = (topic_len < (int)sizeof(topic_buf) - 1)
                  ? topic_len
                  : (int)sizeof(topic_buf) - 1;
    memcpy(topic_buf, topic, copy_len);
    topic_buf[copy_len] = '\0';

    bool matched = false;

    for (int i = 0; i < MQTT_MAX_HANDLERS; i++) {
        if (!s_mqtt.handlers[i].in_use) continue;
        if (topic_matches(s_mqtt.handlers[i].filter, topic_buf)) {
            matched = true;
            /* Release lock before calling back into application code.
             * This prevents deadlock if the callback calls mqtt_service_publish(). */
            xSemaphoreGive(s_mqtt.lock);
            s_mqtt.handlers[i].cb(topic_buf, data, data_len);
            xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);
        }
    }

    if (!matched && s_mqtt.default_handler) {
        xSemaphoreGive(s_mqtt.lock);
        s_mqtt.default_handler(topic_buf, data, data_len);
        xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);
    }

    if (!matched && !s_mqtt.default_handler) {
        ESP_LOGD(__func__, "no handler for topic=%s", topic_buf);
    }
}

/* ═══════════════════════════════════════════════
 * Subscription table  (unchanged logic from v1,
 * kept here for self-contained readability)
 * ═══════════════════════════════════════════════ */
static void restore_subscriptions_locked(void)
{
    for (int i = 0; i < MQTT_MAX_SUBSCRIPTIONS; i++) {
        if (s_mqtt.subs[i].in_use) {
            int id = esp_mqtt_client_subscribe(s_mqtt.client,
                                               s_mqtt.subs[i].topic,
                                               s_mqtt.subs[i].qos);
            ESP_LOGI(__func__, "restore subscribe topic=%s qos=%d msg_id=%d",
                     s_mqtt.subs[i].topic, s_mqtt.subs[i].qos, id);
        }
    }
}

static int find_sub_idx_locked(const char *topic)
{
    for (int i = 0; i < MQTT_MAX_SUBSCRIPTIONS; i++)
        if (s_mqtt.subs[i].in_use && strcmp(s_mqtt.subs[i].topic, topic) == 0)
            return i;
    return -1;
}

static int find_free_sub_idx_locked(void)
{
    for (int i = 0; i < MQTT_MAX_SUBSCRIPTIONS; i++)
        if (!s_mqtt.subs[i].in_use) return i;
    return -1;
}

static esp_err_t save_sub_locked(const char *topic, int qos)
{
    int idx = find_sub_idx_locked(topic);
    if (idx >= 0) { s_mqtt.subs[idx].qos = qos; return ESP_OK; }

    idx = find_free_sub_idx_locked();
    ESP_RETURN_ON_FALSE(idx >= 0, ESP_ERR_NO_MEM, __func__, "subscription table full");

    memset(&s_mqtt.subs[idx], 0, sizeof(s_mqtt.subs[idx]));
    strlcpy(s_mqtt.subs[idx].topic, topic, sizeof(s_mqtt.subs[idx].topic));
    s_mqtt.subs[idx].qos    = qos;
    s_mqtt.subs[idx].in_use = true;
    return ESP_OK;
}

static esp_err_t remove_sub_locked(const char *topic)
{
    int idx = find_sub_idx_locked(topic);
    ESP_RETURN_ON_FALSE(idx >= 0, ESP_ERR_NOT_FOUND, __func__, "topic not subscribed");
    memset(&s_mqtt.subs[idx], 0, sizeof(s_mqtt.subs[idx]));
    return ESP_OK;
}

/* ═══════════════════════════════════════════════
 * URI builder
 * ═══════════════════════════════════════════════ */
static const char *transport_scheme(mqtt_transport_t t)
{
    switch (t) {
        case MQTT_TRANSPORT_TCP: return "mqtt";
        case MQTT_TRANSPORT_TLS: return "mqtts";
        case MQTT_TRANSPORT_WS:  return "ws";
        case MQTT_TRANSPORT_WSS: return "wss";
        default:                 return NULL;
    }
}

static bool transport_is_secure(mqtt_transport_t t)
{
    return (t == MQTT_TRANSPORT_TLS || t == MQTT_TRANSPORT_WSS);
}

static esp_err_t build_uri_locked(void)
{
    const char *scheme = transport_scheme(s_mqtt.cfg.transport);
    ESP_RETURN_ON_FALSE(scheme, ESP_ERR_INVALID_ARG, __func__, "invalid transport");
    ESP_RETURN_ON_FALSE(s_mqtt.cfg.host, ESP_ERR_INVALID_ARG, __func__, "host is null");

    bool has_path = (s_mqtt.cfg.path && strlen(s_mqtt.cfg.path) > 0);
    bool is_ws    = (s_mqtt.cfg.transport == MQTT_TRANSPORT_WS ||
                     s_mqtt.cfg.transport == MQTT_TRANSPORT_WSS);

    int n = has_path && is_ws
          ? snprintf(s_mqtt.uri, sizeof(s_mqtt.uri), "%s://%s:%u%s",
                     scheme, s_mqtt.cfg.host, s_mqtt.cfg.port, s_mqtt.cfg.path)
          : snprintf(s_mqtt.uri, sizeof(s_mqtt.uri), "%s://%s:%u",
                     scheme, s_mqtt.cfg.host, s_mqtt.cfg.port);

    ESP_RETURN_ON_FALSE(n > 0 && (size_t)n < sizeof(s_mqtt.uri),
                        ESP_ERR_INVALID_SIZE, __func__, "URI too long");
    return ESP_OK;
}

/* ═══════════════════════════════════════════════
 * Error logging
 * ═══════════════════════════════════════════════ */
static void log_transport_error(esp_mqtt_event_handle_t event)
{
    if (!event->error_handle) {
        ESP_LOGE(__func__, "MQTT_EVENT_ERROR: null error_handle");
        return;
    }
    switch (event->error_handle->error_type) {
        case MQTT_ERROR_TYPE_TCP_TRANSPORT:
            ESP_LOGE(__func__, "TCP/TLS error esp_tls=0x%x stack=0x%x sock=%d",
                     event->error_handle->esp_tls_last_esp_err,
                     event->error_handle->esp_tls_stack_err,
                     event->error_handle->esp_transport_sock_errno);
            break;
        case MQTT_ERROR_TYPE_CONNECTION_REFUSED:
            ESP_LOGE(__func__, "Connection refused code=0x%x",
                     event->error_handle->connect_return_code);
            break;
        default:
            ESP_LOGE(__func__, "MQTT error type=0x%x", event->error_handle->error_type);
    }
}

/* ═══════════════════════════════════════════════
 * MQTT event handler
 * ═══════════════════════════════════════════════ */
static void mqtt_event_handler(void *arg,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg; (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {

        case MQTT_EVENT_CONNECTED:
            xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);
            s_mqtt.state = MQTT_STATE_CONNECTED;
            ESP_LOGI(__func__, "MQTT connected (uri=%s)", s_mqtt.uri);
            restore_subscriptions_locked();
            xSemaphoreGive(s_mqtt.lock);
            break;

        case MQTT_EVENT_DISCONNECTED:
            xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);
            s_mqtt.state = MQTT_STATE_DISCONNECTED;
            xSemaphoreGive(s_mqtt.lock);
            ESP_LOGW(__func__, "MQTT disconnected");
            break;

        case MQTT_EVENT_DATA:
            
            // just for debugging
            // ESP_LOGI("MQTT_RAW", "topic=%.*s data=%.*s", event->topic_len, event->topic, event->data_len, event->data);

            /* Dispatch inside the lock so handler table is stable.
             * dispatch_message_locked() temporarily drops the lock
             * before each callback to prevent publish() deadlock. */
            xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);
            dispatch_message_locked(event->topic,   event->topic_len,
                                    event->data,    event->data_len);
            xSemaphoreGive(s_mqtt.lock);
            break;

        case MQTT_EVENT_ERROR:
            xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);
            s_mqtt.state = MQTT_STATE_ERROR;
            xSemaphoreGive(s_mqtt.lock);
            log_transport_error(event);
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(__func__, "subscribed msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(__func__, "unsubscribed msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(__func__, "published msg_id=%d", event->msg_id);
            break;

        default:
            ESP_LOGD(__func__, "mqtt event id=%" PRIi32, event_id);
            break;
    }
}

/* ═══════════════════════════════════════════════
 * Client lifecycle  (internal, call with lock held)
 * ═══════════════════════════════════════════════ */
static esp_err_t create_client_locked(void)
{
    esp_err_t err = build_uri_locked();
    ESP_RETURN_ON_ERROR(err, __func__, "URI build failed");

    esp_mqtt_client_config_t c = {
        .broker.address.uri                  = s_mqtt.uri,
        .session.keepalive                   = s_mqtt.cfg.keepalive_sec,
        .session.disable_clean_session       = !s_mqtt.cfg.clean_session,
        .network.disable_auto_reconnect      = !s_mqtt.cfg.auto_reconnect,
        .credentials.client_id               = s_mqtt.cfg.client_id,
        .credentials.username                = s_mqtt.cfg.username,
        .credentials.authentication.password = s_mqtt.cfg.password,
    };

    if (transport_is_secure(s_mqtt.cfg.transport)) {
        ESP_RETURN_ON_FALSE(s_mqtt.cfg.ca_cert, ESP_ERR_INVALID_ARG, __func__,
                            "ca_cert required for TLS/WSS");
        c.broker.verification.certificate               = s_mqtt.cfg.ca_cert;
        c.broker.verification.skip_cert_common_name_check = false;
    }

    s_mqtt.client = esp_mqtt_client_init(&c);
    ESP_RETURN_ON_FALSE(s_mqtt.client, ESP_FAIL, __func__, "esp_mqtt_client_init failed");

    ESP_RETURN_ON_ERROR(
        esp_mqtt_client_register_event(s_mqtt.client, ESP_EVENT_ANY_ID,
                                       mqtt_event_handler, NULL),
        __func__, "register event failed"
    );

    return ESP_OK;
}

static void destroy_client_locked(void)
{
    if (!s_mqtt.client) return;
    if (s_mqtt.started) esp_mqtt_client_stop(s_mqtt.client);
    esp_mqtt_client_destroy(s_mqtt.client);
    s_mqtt.client  = NULL;
    s_mqtt.started = false;
    s_mqtt.state   = MQTT_STATE_STOPPED;
}

/* ═══════════════════════════════════════════════
 * Public API — Lifecycle
 * ═══════════════════════════════════════════════ */
esp_err_t mqtt_service_init(const mqtt_service_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg,       ESP_ERR_INVALID_ARG,   __func__, "cfg is null");
    ESP_RETURN_ON_FALSE(cfg->host, ESP_ERR_INVALID_ARG,   __func__, "host is null");
    ESP_RETURN_ON_FALSE(!s_mqtt.initialized, ESP_ERR_INVALID_STATE, __func__, "already initialized");

    memset(&s_mqtt, 0, sizeof(s_mqtt));

    s_mqtt.lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_mqtt.lock, ESP_ERR_NO_MEM, __func__, "mutex create failed");

    esp_err_t err = copy_config(&s_mqtt.cfg, cfg);
    if (err != ESP_OK) {
        vSemaphoreDelete(s_mqtt.lock);
        s_mqtt.lock = NULL;
        return err;
    }

    s_mqtt.state       = MQTT_STATE_STOPPED;
    s_mqtt.initialized = true;
    return ESP_OK;
}

esp_err_t mqtt_service_start(void)
{
    ESP_RETURN_ON_FALSE(s_mqtt.initialized,  ESP_ERR_INVALID_STATE, __func__, "not initialized");

    xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);

    if (s_mqtt.started) {
        xSemaphoreGive(s_mqtt.lock);
        return ESP_ERR_INVALID_STATE;
    }

    s_mqtt.state = MQTT_STATE_STARTING;

    esp_err_t err = create_client_locked();
    if (err != ESP_OK) {
        s_mqtt.state = MQTT_STATE_ERROR;
        xSemaphoreGive(s_mqtt.lock);
        return err;
    }

    err = esp_mqtt_client_start(s_mqtt.client);
    if (err == ESP_OK) {
        s_mqtt.started = true;
    } else {
        s_mqtt.state = MQTT_STATE_ERROR;
        esp_mqtt_client_destroy(s_mqtt.client);
        s_mqtt.client = NULL;
    }

    xSemaphoreGive(s_mqtt.lock);
    return err;
}

esp_err_t mqtt_service_stop(void)
{
    ESP_RETURN_ON_FALSE(s_mqtt.initialized, ESP_ERR_INVALID_STATE, __func__, "not initialized");

    xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);

    if (!s_mqtt.started || !s_mqtt.client) {
        xSemaphoreGive(s_mqtt.lock);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_mqtt_client_stop(s_mqtt.client);
    s_mqtt.started = false;
    s_mqtt.state   = MQTT_STATE_STOPPED;

    xSemaphoreGive(s_mqtt.lock);
    return err;
}

esp_err_t mqtt_service_deinit(void)
{
    ESP_RETURN_ON_FALSE(s_mqtt.initialized, ESP_ERR_INVALID_STATE, __func__, "not initialized");

    xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);
    destroy_client_locked();
    free_config_strings(&s_mqtt.cfg);
    s_mqtt.initialized = false;
    xSemaphoreGive(s_mqtt.lock);

    vSemaphoreDelete(s_mqtt.lock);
    s_mqtt.lock = NULL;

    memset(&s_mqtt, 0, sizeof(s_mqtt));
    return ESP_OK;
}

esp_err_t mqtt_service_reconfigure(const mqtt_service_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg,              ESP_ERR_INVALID_ARG,   __func__, "cfg is null");
    ESP_RETURN_ON_FALSE(s_mqtt.initialized, ESP_ERR_INVALID_STATE, __func__, "not initialized");

    xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);

    /* tear down existing client */
    destroy_client_locked();

    /* swap config — deep-copies new strings, frees old ones */
    esp_err_t err = copy_config(&s_mqtt.cfg, cfg);
    if (err != ESP_OK) {
        s_mqtt.state = MQTT_STATE_ERROR;
        xSemaphoreGive(s_mqtt.lock);
        return err;
    }

    /* rebuild and restart — subscription table is preserved */
    s_mqtt.state = MQTT_STATE_STARTING;
    err = create_client_locked();
    if (err != ESP_OK) {
        s_mqtt.state = MQTT_STATE_ERROR;
        xSemaphoreGive(s_mqtt.lock);
        return err;
    }

    err = esp_mqtt_client_start(s_mqtt.client);
    if (err == ESP_OK) {
        s_mqtt.started = true;
    } else {
        s_mqtt.state = MQTT_STATE_ERROR;
        esp_mqtt_client_destroy(s_mqtt.client);
        s_mqtt.client = NULL;
    }

    xSemaphoreGive(s_mqtt.lock);
    return err;
}

/* ═══════════════════════════════════════════════
 * Public API — Publish / Subscribe
 * ═══════════════════════════════════════════════ */
esp_err_t mqtt_service_publish(const char *topic,
                               const char *payload,
                               int qos, int retain)
{
    ESP_RETURN_ON_FALSE(topic,   ESP_ERR_INVALID_ARG,   __func__, "topic is null");
    ESP_RETURN_ON_FALSE(payload, ESP_ERR_INVALID_ARG,   __func__, "payload is null");
    ESP_RETURN_ON_FALSE(s_mqtt.initialized, ESP_ERR_INVALID_STATE, __func__, "not initialized");

    xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);

    if (!s_mqtt.client || s_mqtt.state != MQTT_STATE_CONNECTED) {
        xSemaphoreGive(s_mqtt.lock);
        return ESP_ERR_INVALID_STATE;
    }

    int id = esp_mqtt_client_publish(s_mqtt.client, topic, payload, 0, qos, retain);
    xSemaphoreGive(s_mqtt.lock);

    if (id < 0) { ESP_LOGE(__func__, "publish failed topic=%s", topic); return ESP_FAIL; }
    ESP_LOGD(__func__, "publish queued topic=%s msg_id=%d", topic, id);
    return ESP_OK;
}

esp_err_t mqtt_service_subscribe(const char *topic, int qos)
{
    ESP_RETURN_ON_FALSE(topic, ESP_ERR_INVALID_ARG, __func__, "topic is null");
    ESP_RETURN_ON_FALSE(strlen(topic) < MQTT_MAX_TOPIC_LEN,
                        ESP_ERR_INVALID_ARG, __func__, "topic too long");
    ESP_RETURN_ON_FALSE(s_mqtt.initialized, ESP_ERR_INVALID_STATE, __func__, "not initialized");

    xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);

    esp_err_t err = save_sub_locked(topic, qos);
    if (err != ESP_OK) { xSemaphoreGive(s_mqtt.lock); return err; }

    if (s_mqtt.client && s_mqtt.state == MQTT_STATE_CONNECTED) {
        int id = esp_mqtt_client_subscribe(s_mqtt.client, topic, qos);
        if (id < 0) {
            xSemaphoreGive(s_mqtt.lock);
            ESP_LOGE(__func__, "subscribe failed topic=%s", topic);
            return ESP_FAIL;
        }
        ESP_LOGI(__func__, "subscribed topic=%s qos=%d msg_id=%d", topic, qos, id);
    } else {
        ESP_LOGW(__func__, "not connected — stored for reconnect: %s", topic);
    }

    xSemaphoreGive(s_mqtt.lock);
    return ESP_OK;
}

esp_err_t mqtt_service_unsubscribe(const char *topic)
{
    ESP_RETURN_ON_FALSE(topic, ESP_ERR_INVALID_ARG, __func__, "topic is null");
    ESP_RETURN_ON_FALSE(s_mqtt.initialized, ESP_ERR_INVALID_STATE, __func__, "not initialized");

    xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);

    esp_err_t err = remove_sub_locked(topic);
    if (err != ESP_OK) { xSemaphoreGive(s_mqtt.lock); return err; }

    if (s_mqtt.client && s_mqtt.state == MQTT_STATE_CONNECTED) {
        int id = esp_mqtt_client_unsubscribe(s_mqtt.client, topic);
        if (id < 0) {
            xSemaphoreGive(s_mqtt.lock);
            ESP_LOGE(__func__, "unsubscribe failed topic=%s", topic);
            return ESP_FAIL;
        }
        ESP_LOGI(__func__, "unsubscribed topic=%s msg_id=%d", topic, id);
    }

    xSemaphoreGive(s_mqtt.lock);
    return ESP_OK;
}

/* ═══════════════════════════════════════════════
 * Public API — Handler registration
 * ═══════════════════════════════════════════════ */
esp_err_t mqtt_service_register_handler(const char *filter, mqtt_rx_cb_t cb)
{
    ESP_RETURN_ON_FALSE(filter, ESP_ERR_INVALID_ARG, __func__, "filter is null");
    ESP_RETURN_ON_FALSE(cb,     ESP_ERR_INVALID_ARG, __func__, "cb is null");
    ESP_RETURN_ON_FALSE(strlen(filter) < MQTT_MAX_TOPIC_LEN,
                        ESP_ERR_INVALID_ARG, __func__, "filter too long");

    xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);

    /* update existing entry if filter already registered */
    for (int i = 0; i < MQTT_MAX_HANDLERS; i++) {
        if (s_mqtt.handlers[i].in_use &&
            strcmp(s_mqtt.handlers[i].filter, filter) == 0) {
            s_mqtt.handlers[i].cb = cb;
            xSemaphoreGive(s_mqtt.lock);
            return ESP_OK;
        }
    }

    /* find free slot */
    for (int i = 0; i < MQTT_MAX_HANDLERS; i++) {
        if (!s_mqtt.handlers[i].in_use) {
            strlcpy(s_mqtt.handlers[i].filter, filter,
                    sizeof(s_mqtt.handlers[i].filter));
            s_mqtt.handlers[i].cb     = cb;
            s_mqtt.handlers[i].in_use = true;
            xSemaphoreGive(s_mqtt.lock);
            ESP_LOGI(__func__, "handler registered filter=%s", filter);
            return ESP_OK;
        }
    }

    xSemaphoreGive(s_mqtt.lock);
    ESP_LOGE(__func__, "handler table full (max=%d)", MQTT_MAX_HANDLERS);
    return ESP_ERR_NO_MEM;
}

esp_err_t mqtt_service_unregister_handler(const char *filter)
{
    ESP_RETURN_ON_FALSE(filter, ESP_ERR_INVALID_ARG, __func__, "filter is null");

    xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);

    for (int i = 0; i < MQTT_MAX_HANDLERS; i++) {
        if (s_mqtt.handlers[i].in_use &&
            strcmp(s_mqtt.handlers[i].filter, filter) == 0) {
            memset(&s_mqtt.handlers[i], 0, sizeof(s_mqtt.handlers[i]));
            xSemaphoreGive(s_mqtt.lock);
            ESP_LOGI(__func__, "handler unregistered filter=%s", filter);
            return ESP_OK;
        }
    }

    xSemaphoreGive(s_mqtt.lock);
    return ESP_ERR_NOT_FOUND;
}

void mqtt_service_set_default_handler(mqtt_rx_cb_t cb)
{
    xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);
    s_mqtt.default_handler = cb;
    xSemaphoreGive(s_mqtt.lock);
}

/* ═══════════════════════════════════════════════
 * Public API — Status
 * ═══════════════════════════════════════════════ */
bool mqtt_service_is_connected(void)
{
    if (!s_mqtt.initialized || !s_mqtt.lock) return false;
    xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);
    bool c = (s_mqtt.state == MQTT_STATE_CONNECTED);
    xSemaphoreGive(s_mqtt.lock);
    return c;
}

mqtt_state_t mqtt_service_get_state(void)
{
    if (!s_mqtt.initialized || !s_mqtt.lock) return MQTT_STATE_STOPPED;
    xSemaphoreTake(s_mqtt.lock, portMAX_DELAY);
    mqtt_state_t st = s_mqtt.state;
    xSemaphoreGive(s_mqtt.lock);
    return st;
}
