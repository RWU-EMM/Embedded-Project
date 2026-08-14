#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

/* ========================= Hardware configuration ========================= */

#define CMD_GPIO_PIN              (2U)
#define RDY_GPIO_PIN              (3U)

/* Nano 33 IoT hardware UART: D0 = RX, D1 = TX, Serial1/SERCOM5. */
#define UART_BAUD_RATE            (115200UL)
#define UART_DATA_BITS            (8U)
#define UART_STOP_BITS            (1U)
#define UART_PARITY               (0U) /* 0 = none, 1 = even, 2 = odd */

#define ADC_INPUT_PIN             (A1)
#define ADC_RESOLUTION_BITS       (12U)

/* The Nano 33 IoT has one true DAC on A0/DAC0. */
#define DAC_OUTPUT_PIN            (A0)
#define DAC_RESOLUTION_BITS       (10U)

#define TIMER_DEFAULT_PERIOD_US   (200UL)
#define TIMER_MIN_PERIOD_US       (1UL)
#define TIMER_MAX_PERIOD_US       (1398000UL)

#endif
