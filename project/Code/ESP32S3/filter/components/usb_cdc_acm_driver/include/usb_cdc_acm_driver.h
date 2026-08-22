#ifndef USB_CDC_ACM_DRIVER_H
#define USB_CDC_ACM_DRIVER_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum
{
    USB_CDC_PORT_0 = 0,
    USB_CDC_PORT_1 = 1
} usb_cdc_port_t;

typedef struct
{
    uint8_t enable_port_0;
    uint8_t enable_port_1;
} usb_cdc_driver_config_t;

typedef void (*usb_cdc_rx_callback_t)(
    usb_cdc_port_t port,
    const uint8_t *data,
    size_t length,
    void *user_ctx);

typedef struct
{
    usb_cdc_port_t port;
    usb_cdc_rx_callback_t rx_callback;
    void *user_ctx;
} usb_cdc_port_config_t;

/**
 * @brief Install TinyUSB driver and initialise enabled CDC-ACM ports.
 *        Also spawns the internal flush task — must be called before any
 *        other usb_cdc_* function.
 */
esp_err_t usb_cdc_driver_init(const usb_cdc_driver_config_t *config);

/**
 * @brief Attach an RX callback to an already-initialised port.
 *        Call after usb_cdc_driver_init().
 */
esp_err_t usb_cdc_port_init(const usb_cdc_port_config_t *config);

/**
 * @brief Queue data for transmission.
 *        Non-blocking — returns immediately after enqueuing.
 *        The internal flush task will push bytes to the host within ~1 ms.
 */
esp_err_t usb_cdc_write(usb_cdc_port_t port, const uint8_t *data, size_t length);

/**
 * @brief Returns non-zero when the host has DTR asserted (port open).
 */
uint8_t usb_cdc_is_connected(usb_cdc_port_t port);

#endif /* USB_CDC_ACM_DRIVER_H */