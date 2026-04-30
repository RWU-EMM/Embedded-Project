#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "protocol_examples_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "cJSON.h"

#include "mqtt_service.h"
#include "mqtt_cert.h"

static const char *TAG = "app_main";

/* ─────────────────────────────────────────────
 * Broker connection helper
 *
 * Returns a config struct for the given transport.
 * All strings point to literals / mqtt_ca_cert — they
 * are deep-copied inside mqtt_service_init/reconfigure,
 * so lifetime is not a concern here.
 * ───────────────────────────────────────────── */
static mqtt_service_config_t make_config(mqtt_transport_t transport)
{
    static const struct {
        mqtt_transport_t transport;
        uint16_t         port;
    } PORT_MAP[] = {
        { MQTT_TRANSPORT_TCP, 1883 },
        { MQTT_TRANSPORT_TLS, 8883 },
        { MQTT_TRANSPORT_WS,  8083 },
        { MQTT_TRANSPORT_WSS, 8084 },
    };

    uint16_t port = 8883;   /* default to TLS */
    for (int i = 0; i < 4; i++) {
        if (PORT_MAP[i].transport == transport) { port = PORT_MAP[i].port; break; }
    }

    mqtt_service_config_t cfg = {
        .transport      = transport,
        .host           = "broker.example.com",
        .port           = port,
        .path           = "/mqtt",              /* used by WS / WSS only */
        .client_id      = "esp32s3-node-01",
        .username       = NULL,
        .password       = NULL,
        .ca_cert        = mqtt_ca_cert,         /* NULL for TCP/WS in testing */
        .keepalive_sec  = 60,
        .clean_session  = true,
        .auto_reconnect = true,
    };
    return cfg;
}

/* ─────────────────────────────────────────────
 * Command topic handler
 *
 * Topic:   devices/esp32s3-node-01/cmd
 * Payload: { "transport": "wss" }
 *          { "transport": "tcp" }   etc.
 *
 * NOTE: do NOT call mqtt_service_publish() from inside
 * a handler — the service drops its lock before calling
 * back, so publish is safe, but reconfigure() takes the
 * lock again internally — that is also safe (non-recursive
 * mutex would deadlock; this is fine because the lock is
 * released first).  Either way, doing heavy work in a
 * callback is bad practice — post to a queue instead.
 * ───────────────────────────────────────────── */
static QueueHandle_t s_transport_queue;

typedef struct { mqtt_transport_t transport; } transport_cmd_t;

static void on_cmd(const char *topic, const char *data, int data_len)
{
    ESP_LOGI(TAG, "cmd [%s]: %.*s", topic, data_len, data);

    /* parse JSON */
    char *buf = strndup(data, data_len);
    if (!buf) return;

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) { ESP_LOGW(TAG, "invalid JSON in cmd"); return; }

    cJSON *t_item = cJSON_GetObjectItem(root, "transport");
    if (cJSON_IsString(t_item)) {
        mqtt_transport_t transport = MQTT_TRANSPORT_TLS;   /* default */
        const char *s = t_item->valuestring;

        if      (strcasecmp(s, "tcp") == 0) transport = MQTT_TRANSPORT_TCP;
        else if (strcasecmp(s, "tls") == 0) transport = MQTT_TRANSPORT_TLS;
        else if (strcasecmp(s, "ws")  == 0) transport = MQTT_TRANSPORT_WS;
        else if (strcasecmp(s, "wss") == 0) transport = MQTT_TRANSPORT_WSS;
        else {
            ESP_LOGW(TAG, "unknown transport '%s'", s);
            cJSON_Delete(root);
            return;
        }

        transport_cmd_t cmd = { .transport = transport };
        xQueueSend(s_transport_queue, &cmd, 0);   /* non-blocking post */
        ESP_LOGI(TAG, "transport switch queued → %s", s);
    }

    cJSON_Delete(root);
}

/* ─────────────────────────────────────────────
 * Broadcast handler — example of wildcard filter
 * ───────────────────────────────────────────── */
static void on_broadcast(const char *topic, const char *data, int data_len)
{
    ESP_LOGI(TAG, "broadcast [%s]: %.*s", topic, data_len, data);
    /* handle broadcast commands here */
}

/* ─────────────────────────────────────────────
 * Default handler — catch-all for unmatched topics
 * ───────────────────────────────────────────── */
static void on_unmatched(const char *topic, const char *data, int data_len)
{
    ESP_LOGD(TAG, "unmatched [%s] len=%d", topic, data_len);
}

/* ─────────────────────────────────────────────
 * Transport-switch task
 *
 * Runs mqtt_service_reconfigure() outside of the
 * MQTT callback context.  Keeps callbacks fast.
 * ───────────────────────────────────────────── */
static void transport_switch_task(void *arg)
{
    transport_cmd_t cmd;
    for (;;) {
        if (xQueueReceive(s_transport_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "switching transport to %d", (int)cmd.transport);
            mqtt_service_config_t cfg = make_config(cmd.transport);
            esp_err_t err = mqtt_service_reconfigure(&cfg);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "reconfigure failed: %s", esp_err_to_name(err));
            }
        }
    }
}

/* ─────────────────────────────────────────────
 * Entry point
 * ───────────────────────────────────────────── */
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    /* Queue for transport-switch commands from MQTT callbacks */
    s_transport_queue = xQueueCreate(4, sizeof(transport_cmd_t));
    xTaskCreate(transport_switch_task, "transport_sw", 4096, NULL, 5, NULL);

    /* Start with TLS */
    mqtt_service_config_t cfg = make_config(MQTT_TRANSPORT_TLS);
    ESP_ERROR_CHECK(mqtt_service_init(&cfg));

    /* Register topic handlers BEFORE starting — they survive reconfigure() */
    ESP_ERROR_CHECK(mqtt_service_register_handler(
        "devices/esp32s3-node-01/cmd", on_cmd));

    ESP_ERROR_CHECK(mqtt_service_register_handler(
        "devices/broadcast/#", on_broadcast));   /* wildcard */

    mqtt_service_set_default_handler(on_unmatched);

    ESP_ERROR_CHECK(mqtt_service_start());

    /* Subscribe — stored internally; restored automatically on reconnect */
    ESP_ERROR_CHECK(mqtt_service_subscribe("devices/esp32s3-node-01/cmd", 1));
    ESP_ERROR_CHECK(mqtt_service_subscribe("devices/broadcast/#", 0));

    /* Main loop — periodic telemetry */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (mqtt_service_is_connected()) {
            esp_err_t err = mqtt_service_publish(
                "devices/esp32s3-node-01/status",
                "{\"alive\":true}",
                1, 0);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "publish failed: %s", esp_err_to_name(err));
            }
        }
    }
}
