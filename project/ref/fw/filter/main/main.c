#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "dsm_packet.h"
#include "filter_config.h"   /* coefficient bank + fts_config_t          */
#include "digital_filters.h" /* df_handle_t, df_init(), df_process()     */
#include "uart_driver.h"     /* init_uart_driver(), uart_send()          */

#include "usb_cdc_acm_driver.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "soc/soc_caps.h"

#include "driver/gptimer.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"

/*
 * Build-time switches
 *  */

/** Set to 1 to enable per-tick period/exec-time diagnostics via ESP_LOGI. */
#define ENABLE_DEBUG_LOGS 0

static const char *TAG = "FTS";

/*
 * Sampling-rate configuration
 *  */

#define DEFAULT_FS_HZ (5000U)

static const uint32_t fs_Hz = DEFAULT_FS_HZ;
static const uint32_t FTs_us = (1000000U / DEFAULT_FS_HZ);

/*
 * DDS
 *  */

#define DDS_PHASE_BITS (30U)
#define DDS_PHASE_MAX (1UL << DDS_PHASE_BITS)

static uint32_t s_phase = 0;
static uint32_t s_dphase = 0; /* updated whenever sim freq changes */

/*
 * ADC
 *  */

#define ADC_CHANNEL ADC_CHANNEL_4 /* matches user hardware */
#define ADC_ATTEN ADC_ATTEN_DB_12
#define ADC_BITWIDTH ADC_BITWIDTH_DEFAULT

static adc_oneshot_unit_handle_t s_adc_handle = NULL;

static void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &s_adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL, &chan_cfg));
    ESP_LOGI(TAG, "ADC initialised (unit=1, ch=%d, atten=12dB)", ADC_CHANNEL);
}

/*
 * Digital filter handle
 *  */

static df_handle_t s_filter;

/*
 * Global runtime config
 *   Declared extern in filter_config.h; defined here so the linker sees
 *   exactly one definition.
 *  */

fts_config_t g_fts_cfg = {
    .filter_pending = false,
    .input_pending = false,
    .sim_freq_pending = false,
    .next_filter = DEFAULT_FILTER_TYPE,
    .next_input = DEFAULT_INPUT_MODE,
    .next_sim_freq_hz = DEFAULT_SIM_FREQ_HZ,
    .active_filter = DEFAULT_FILTER_TYPE,
    .active_input = DEFAULT_INPUT_MODE,
    .active_sim_freq_hz = DEFAULT_SIM_FREQ_HZ,
};

/*
 * Pending-change processor  (called once per sampling tick, before DSP work)
 *
 * The UART RX callback runs in the uart_driver's rx_task, which only touches
 * the volatile pending flags and the "next_*" fields.  This function runs in
 * the sampling task and applies them, then calls df_init() if needed.
 * No mutex required for this single-writer / single-reader pattern.
 *  */

static inline void fts_cfg_apply_pending(void)
{
    bool reinit_filter = false;

    if (g_fts_cfg.sim_freq_pending)
    {
        g_fts_cfg.sim_freq_pending = false;
        g_fts_cfg.active_sim_freq_hz = g_fts_cfg.next_sim_freq_hz;
        s_dphase = (uint32_t)((g_fts_cfg.active_sim_freq_hz / (float)fs_Hz) * (float)DDS_PHASE_MAX);
        ESP_LOGI(TAG, "SIM freq -> %.1f Hz (dphase=%lu)", (double)g_fts_cfg.active_sim_freq_hz, (unsigned long)s_dphase);
    }

    if (g_fts_cfg.filter_pending)
    {
        g_fts_cfg.filter_pending = false;
        g_fts_cfg.active_filter = g_fts_cfg.next_filter;
        reinit_filter = true;
    }

    if (g_fts_cfg.input_pending)
    {
        g_fts_cfg.input_pending = false;
        g_fts_cfg.active_input = g_fts_cfg.next_input;
        reinit_filter = true; /* reset filter state on source switch */
        ESP_LOGI(TAG, "Input -> %s", g_fts_cfg.active_input == FTS_INPUT_ADC ? "ADC" : "SIM");
    }

    if (reinit_filter)
    {
        df_init(&s_filter,
                g_fts_cfg.active_filter,
                filter_get_coeff(g_fts_cfg.active_filter),
                DEFAULT_DIRECT_FORM,
                DEFAULT_QUANT_MODE);
        ESP_LOGI(TAG, "Filter reinit -> type %d", (int)g_fts_cfg.active_filter);
    }
}

/*
 * UART RX callback  (called from uart_driver's rx_task with received bytes)
 *
 * Commands (plain ASCII, newline-terminated):
 *   filter <n>       n = 0..7  (filter_type_e)
 *   input sim        switch to DDS simulation
 *   input adc        switch to real ADC
 *   freq <f>         DDS frequency in Hz
 *
 * Only writes volatile fields of g_fts_cfg and sets pending flags.
 * The sampling loop applies the changes safely between ticks.
 *  */

static void uart_cmd_rx_cb(uint8_t *data, uint16_t len)
{
    /* uart_driver null-terminates the buffer before calling us */
    (void)len;
    char *line = (char *)data;

    /* Strip trailing CR / LF */
    char *end = line + strlen(line) - 1;
    while (end >= line && (*end == '\r' || *end == '\n'))
        *end-- = '\0';

    if (strlen(line) == 0)
        return;

    /* --- filter <n> --- */
    if (strncmp(line, "filter ", 7) == 0)
    {
        int n = atoi(line + 7);
        if (n >= 0 && n < (int)_FILTER_TYPE_COUNT)
        {
            fts_cfg_set_filter((filter_type_e)n);
            ESP_LOGI(TAG, "CMD: filter=%d", n);
        }
        else
        {
            ESP_LOGW(TAG, "CMD: invalid filter index %d (valid: 0-%d)",
                     n, (int)_FILTER_TYPE_COUNT - 1);
        }

        /* --- input sim | input adc --- */
    }
    else if (strcmp(line, "input sim") == 0)
    {
        fts_cfg_set_input(FTS_INPUT_SIM);
        ESP_LOGI(TAG, "CMD: input=sim");
    }
    else if (strcmp(line, "input adc") == 0)
    {
        fts_cfg_set_input(FTS_INPUT_ADC);
        ESP_LOGI(TAG, "CMD: input=adc");

        /* --- freq <f> --- */
    }
    else if (strncmp(line, "freq ", 5) == 0)
    {
        float f = strtof(line + 5, NULL);
        if (f > 0.0f && f < (float)(fs_Hz / 2u))
        {
            fts_cfg_set_sim_freq(f);
            ESP_LOGI(TAG, "CMD: sim_freq=%.1f Hz", (double)f);
        }
        else
        {
            ESP_LOGW(TAG, "CMD: invalid freq %.1f (must be 0 < f < %lu Hz)",
                     (double)f, (unsigned long)(fs_Hz / 2u));
        }
    }
    else
    {
        ESP_LOGW(TAG, "CMD: unknown command '%s'", line);
    }
}

/*
 * DSM packet state
 *  */

static dsm_packet_t s_dsm_pkt;
static int16_t s_seq = 0;

_Static_assert(sizeof(dsm_packet_t) == DSM_PACKET_SIZE_BYTES, "DSM size invalid");

/*
 * Timer ISR
 *  */

static TaskHandle_t s_fts_task = NULL;

static bool IRAM_ATTR timer_on_alarm_cb(gptimer_handle_t timer,
                                        const gptimer_alarm_event_data_t *edata,
                                        void *user_ctx)
{
    (void)timer;
    (void)edata;
    (void)user_ctx;
    BaseType_t high_task_awoken = pdFALSE;
    vTaskNotifyGiveFromISR(s_fts_task, &high_task_awoken);
    return (high_task_awoken == pdTRUE);
}

/*
 * app_main
 *  */

void app_main(void)
{
    memset(&s_dsm_pkt, 0, sizeof(s_dsm_pkt));
    s_dsm_pkt.sig = DSM_SIGNATURE;

    s_fts_task = xTaskGetCurrentTaskHandle();

    /* ------------------------------------------------------------------
     * USB CDC
     * ---------------------------------------------------------------- */
    usb_cdc_driver_config_t usb_cfg = {
        .enable_port_0 = true,
        .enable_port_1 = false,
    };
    ESP_ERROR_CHECK(usb_cdc_driver_init(&usb_cfg));

    /* ------------------------------------------------------------------
     * ADC  (always init so it is ready when "input adc" is commanded)
     * ---------------------------------------------------------------- */
    adc_init();

    /* ------------------------------------------------------------------
     * UART command interface  (uses your uart_driver component)
     *   UART1, 115200-8N1, GPIO 17 TX / 18 RX  – adjust pins as needed
     * ---------------------------------------------------------------- */
    static uart_driver_handle_t s_uart_cmd;
    memset(&s_uart_cmd, 0, sizeof(s_uart_cmd));

    s_uart_cmd.config = (uart_driver_config_t){
        .uart_port = UART_NUM_0,
        .baudrate = _115200,
        .data_bits = UART_DATA_8_BITS,
        .stop_bits = UART_STOP_BITS_1,
        .parity = UART_PARITY_DISABLE,
        .tx_pin = -1, /* <-- adjust to your board */
        .rx_pin = -1, /* <-- adjust to your board */
    };
    s_uart_cmd.rx_cb = uart_cmd_rx_cb;
    ESP_ERROR_CHECK(init_uart_driver(&s_uart_cmd));
    ESP_LOGI(TAG, "UART command interface ready (UART%d, 115200-8N1)",
             s_uart_cmd.config.uart_port);

    /* ------------------------------------------------------------------
     * DDS initial phase increment
     * ---------------------------------------------------------------- */
    s_dphase = (uint32_t)((DEFAULT_SIM_FREQ_HZ / (float)fs_Hz) * (float)DDS_PHASE_MAX);

    /* ------------------------------------------------------------------
     * Digital filter  (initial type and coefficients from filter_config.h)
     * ---------------------------------------------------------------- */
    df_init(&s_filter,
            DEFAULT_FILTER_TYPE,
            filter_get_coeff(DEFAULT_FILTER_TYPE),
            DEFAULT_DIRECT_FORM,
            DEFAULT_QUANT_MODE);

    ESP_LOGI(TAG, "Boot: filter type=%d  input=%s  sim_freq=%.1f Hz",
             (int)DEFAULT_FILTER_TYPE,
             DEFAULT_INPUT_MODE == FTS_INPUT_ADC ? "ADC" : "SIM",
             (double)DEFAULT_SIM_FREQ_HZ);

    /* ------------------------------------------------------------------
     * GP timer  (fires every Ts = 1/Fs)
     * ---------------------------------------------------------------- */
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, /* 1 tick = 1 µs */
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = FTs_us,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

    gptimer_event_callbacks_t cbs = {.on_alarm = timer_on_alarm_cb};
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(gptimer));
    ESP_ERROR_CHECK(gptimer_start(gptimer));

    /* ------------------------------------------------------------------
     * Sampling loop
     * ---------------------------------------------------------------- */
#if (ENABLE_DEBUG_LOGS == 1)
    static int64_t last_t = 0;
    static int64_t period_sum = 0;
    static int64_t exec_sum = 0;
    static uint32_t cnt = 0;
#endif

    while (1)
    {
        /* Block until the timer ISR fires (every Ts = 1/Fs) */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* Apply any pending runtime configuration changes */
        fts_cfg_apply_pending();

#if (ENABLE_DEBUG_LOGS == 1)
        int64_t now = esp_timer_get_time();
        if (last_t != 0)
            period_sum += (now - last_t);
        last_t = now;

#endif
        int64_t t1 = esp_timer_get_time();
        /* ==
         * INPUT ACQUISITION
         *  */
        int16_t adc_raw;
        float phase_norm = 0.0f;
        float sine = 0.0f;

        if (g_fts_cfg.active_input == FTS_INPUT_ADC)
        {
            /* ---- Real ADC path ---------------------------------------- */
            int raw = 0;
            ESP_ERROR_CHECK(adc_oneshot_read(s_adc_handle, ADC_CHANNEL, &raw));
            /* raw is 0..4095 for 12-bit.  Centre around zero. */
            adc_raw = (int16_t)(raw - 2048);
        }
        else
        {
            /* ---- DDS simulation path ---------------------------------- */
            s_phase += s_dphase;
            phase_norm = (float)s_phase / (float)DDS_PHASE_MAX;
            sine = sinf(2.0f * (float)M_PI * phase_norm);
            adc_raw = (int16_t)(2000.0f * sine);
        }

        /* ==
         * DIGITAL FILTER
         *  */
        int16_t filt_y = df_process(&s_filter, adc_raw);

        /* ==
         * BUILD DSM PACKET
         *  */
        s_dsm_pkt.n = (uint8_t)(((DSM_PACKET_SIZE_BYTES) & 0x3F) |
                                (((s_seq++) & 0x3) << 6));

        s_dsm_pkt.adc_in = adc_raw;
        s_dsm_pkt.fgen_out = adc_raw;
        s_dsm_pkt.filt_x = adc_raw;
        s_dsm_pkt.filt_y = filt_y;
        s_dsm_pkt.dac0 = (int16_t)(adc_raw + 2048);
        s_dsm_pkt.dac1 = (int16_t)(filt_y + 2048);

        /* Auxiliary float channels */
        s_dsm_pkt.aux0 = (g_fts_cfg.active_input == FTS_INPUT_SIM) ? phase_norm : 0.0f;
        s_dsm_pkt.aux1 = (g_fts_cfg.active_input == FTS_INPUT_SIM) ? sine : 0.0f;
        s_dsm_pkt.aux2 = (float)filt_y;
        s_dsm_pkt.aux3 = (float)g_fts_cfg.active_filter; /* active filter ID  */
        s_dsm_pkt.aux4 = (float)g_fts_cfg.active_input;  /* input source flag */
        s_dsm_pkt.aux5 = g_fts_cfg.active_sim_freq_hz;   /* DDS frequency     */
                                                         /* aux6..aux15: reserved for future use */

        int64_t dt_us = esp_timer_get_time() - t1;
        s_dsm_pkt.dt = (int16_t)dt_us;
        s_dsm_pkt.crc16 = dsm_calc_xor16((const uint8_t *)&s_dsm_pkt.sig,
                                         DSM_PACKET_SIZE_BYTES - 2);

        /* ==
         * TRANSMIT OVER USB CDC
         *  */
        if (usb_cdc_is_connected(USB_CDC_PORT_0))
        {
            usb_cdc_write(USB_CDC_PORT_0,
                          (const uint8_t *)&s_dsm_pkt,
                          sizeof(s_dsm_pkt));
        }

        /* ==
         * DIAGNOSTICS  (compiled out when ENABLE_DEBUG_LOGS == 0)
         *  */
#if (ENABLE_DEBUG_LOGS == 1)
        exec_sum += (esp_timer_get_time() - t1);
        cnt++;
        if (cnt >= 20)
        {
            ESP_LOGI(TAG,
                     "avg_period=%lld us  avg_exec=%lld us  filt=%d  input=%s",
                     period_sum / 19LL,
                     exec_sum / 20LL,
                     (int)g_fts_cfg.active_filter,
                     g_fts_cfg.active_input == FTS_INPUT_ADC ? "ADC" : "SIM");
            cnt = period_sum = exec_sum = 0;
        }
#endif
    }
}
