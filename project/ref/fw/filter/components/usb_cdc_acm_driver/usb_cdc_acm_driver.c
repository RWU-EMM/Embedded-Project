#include "usb_cdc_acm_driver.h"

#include <string.h>

#include "esp_log.h"
#include "esp_check.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "sdkconfig.h"

/* ------------------------------------------------------------------ */
/*  Private types                                                       */
/* ------------------------------------------------------------------ */

typedef struct
{
    uint8_t initialized;
    uint8_t connected;

    usb_cdc_rx_callback_t rx_callback;
    void *user_ctx;

    uint8_t rx_buffer[CONFIG_TINYUSB_CDC_RX_BUFSIZE];
} usb_cdc_context_t;

/* ------------------------------------------------------------------ */
/*  Module state                                                        */
/* ------------------------------------------------------------------ */

static usb_cdc_context_t s_context[2];

/* ------------------------------------------------------------------ */
/*  Port / interface mapping helpers                                    */
/* ------------------------------------------------------------------ */

static inline tinyusb_cdcacm_itf_t prv_map_port(usb_cdc_port_t port)
{
    return (port == USB_CDC_PORT_1) ? TINYUSB_CDC_ACM_1 : TINYUSB_CDC_ACM_0;
}

static inline usb_cdc_port_t prv_map_interface(int itf)
{
    return (itf == TINYUSB_CDC_ACM_1) ? USB_CDC_PORT_1 : USB_CDC_PORT_0;
}

/* ------------------------------------------------------------------ */
/*  TinyUSB callbacks                                                   */
/* ------------------------------------------------------------------ */

static void prv_rx_callback(int itf, cdcacm_event_t *event)
{
    (void)event;

    usb_cdc_port_t port = prv_map_interface(itf);
    usb_cdc_context_t *ctx = &s_context[port];
    size_t rx_size = 0;

    esp_err_t err = tinyusb_cdcacm_read(
        itf, ctx->rx_buffer, sizeof(ctx->rx_buffer), &rx_size);

    if (err != ESP_OK)
    {
        ESP_LOGE("usb_cdc", "read error on port %d", port);
        return;
    }

    if ((rx_size > 0U) && (ctx->rx_callback != NULL))
    {
        ctx->rx_callback(port, ctx->rx_buffer, rx_size, ctx->user_ctx);
    }
}

static void prv_line_state_callback(int itf, cdcacm_event_t *event)
{
    usb_cdc_port_t port = prv_map_interface(itf);
    usb_cdc_context_t *ctx = &s_context[port];

    ctx->connected = event->line_state_changed_data.dtr;

    ESP_LOGI("usb_cdc", "port=%d dtr=%d rts=%d",
             port,
             event->line_state_changed_data.dtr,
             event->line_state_changed_data.rts);
}

/* ------------------------------------------------------------------ */
/*  Flush task                                                          */
/*                                                                      */
/*  Runs every 1 ms with a non-blocking flush (timeout = 0).           */
/*  This is the ONLY place write_flush is called, keeping the write    */
/*  path completely non-blocking for the caller.                        */
/* ------------------------------------------------------------------ */

static void prv_flush_task(void *arg)
{
    (void)arg;

    while (1)
    {
        for (int i = 0; i < 2; i++)
        {
            if (s_context[i].initialized)
            {
                /* timeout = 0 → non-blocking; just pushes whatever is   */
                /* already queued in the TinyUSB FIFO to the USB layer.  */
                esp_err_t err = tinyusb_cdcacm_write_flush(
                    prv_map_port((usb_cdc_port_t)i), 0);

                if ((err != ESP_OK) && (err != ESP_ERR_TIMEOUT))
                {
                    ESP_LOGW("usb_cdc", "flush port %d: %s",
                             i, esp_err_to_name(err));
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* ------------------------------------------------------------------ */
/*  Internal port init                                                  */
/* ------------------------------------------------------------------ */

static esp_err_t prv_port_init(usb_cdc_port_t port)
{
    tinyusb_config_cdcacm_t cfg = {
        .cdc_port = prv_map_port(port),
        .callback_rx = prv_rx_callback,
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = prv_line_state_callback,
        .callback_line_coding_changed = NULL,
    };

    ESP_RETURN_ON_ERROR(
        tinyusb_cdcacm_init(&cfg), "usb_cdc", "cdcacm init port %d failed", port);

    s_context[port].initialized = 1;

    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */

esp_err_t usb_cdc_driver_init(const usb_cdc_driver_config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    /* Install TinyUSB driver */
    const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    ESP_RETURN_ON_ERROR(
        tinyusb_driver_install(&tusb_cfg), "usb_cdc", "tinyusb install failed");

    /* Initialise requested CDC-ACM ports */
    if (config->enable_port_0)
    {
        ESP_RETURN_ON_ERROR(
            prv_port_init(USB_CDC_PORT_0), "usb_cdc", "port 0 init failed");
    }

#if (CONFIG_TINYUSB_CDC_COUNT > 1)
    if (config->enable_port_1)
    {
        ESP_RETURN_ON_ERROR(
            prv_port_init(USB_CDC_PORT_1), "usb_cdc", "port 1 init failed");
    }
#endif

    /*
     * Spawn the flush task on core 0 (same as TinyUSB) at a priority just
     * above the idle task so it yields readily.  A 2 kB stack is sufficient.
     */
    BaseType_t ret = xTaskCreatePinnedToCore(
        prv_flush_task, "usb_flush", 2048, NULL, 2, NULL, 0);

    if (ret != pdPASS)
    {
        ESP_LOGE("usb_cdc", "flush task creation failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t usb_cdc_port_init(const usb_cdc_port_config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    usb_cdc_context_t *ctx = &s_context[config->port];

    if (!ctx->initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

    ctx->rx_callback = config->rx_callback;
    ctx->user_ctx = config->user_ctx;

    return ESP_OK;
}

/*
 * usb_cdc_write — enqueue only, never blocks.
 *
 * The flush task takes care of pushing bytes to USB.  At 5 kHz the caller
 * enqueues 82 bytes every 200 µs; the 1 ms flush cadence is fast enough to
 * drain a burst of up to 5 packets before the next batch arrives.
 */
esp_err_t usb_cdc_write(usb_cdc_port_t port, const uint8_t *data, size_t length)
{
    if ((data == NULL) || (length == 0U))
    {
        return ESP_ERR_INVALID_ARG;
    }

    int ret = tinyusb_cdcacm_write_queue(prv_map_port(port), data, length);

    if (ret < 0)
    {
        return ESP_FAIL;
    }

    if ((size_t)ret != length)
    {
        return ESP_ERR_NO_MEM; // queue full / partial queue
    }

    return ESP_OK;
}

uint8_t usb_cdc_is_connected(usb_cdc_port_t port)
{
    return s_context[port].connected;
}