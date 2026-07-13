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
#include "coeff_loader.h"
#include "fgen_sim.h"

#include "wifi_udp_handler.h"

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

#if (ACTIVE_SEND_MODE == SEND_MODE_UDP)
static uint8_t s_batch_buf[sizeof(dsm_batch_hdr_t) + UDP_BATCH_SIZE * sizeof(dsm_packet_t)] = {0};
static uint32_t s_batch_idx = 0;
static uint32_t s_batch_id = 0;
_Static_assert(sizeof(dsm_batch_hdr_t) == 6, "batch hdr size invalid");
#endif

#define DEFAULT_SIM_AMP 100
#define DEFAULT_SIM_MODE _FGEN_SIM_IMP

gptimer_handle_t gptimer = NULL;
gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT,
    .direction = GPTIMER_COUNT_UP,
    .resolution_hz = 1000000, /* 1 tick = 1 µs */
};

const void *g_coeff_override = NULL;
// Build-time switches
// Set to 1 to enable per-tick period/exec-time diagnostics via ESP_LOGI.
#define ENABLE_DEBUG_LOGS 0

// Sampling-rate configuration
#define DEFAULT_FS_HZ (5000U)

static const uint32_t fs_Hz = DEFAULT_FS_HZ;
// 2 ticks required for ISR scheduling
static const uint32_t FTs_us = (1000000U / DEFAULT_FS_HZ) - 2;

// ADC
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
    ESP_LOGI(__func__, "ADC initialised (unit=1, ch=%d, atten=12dB)", ADC_CHANNEL);
}
// Digital filter handle
static df_handle_t s_filter;

static fgen_sim_t s_fgen;

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
    .timer_state = FTS_TIMER_RUNNING,
    .sim_mode_pending = false,
    .next_sim_mode = DEFAULT_SIM_MODE,
    .active_sim_mode = DEFAULT_SIM_MODE,
    .sim_amp_pending = false,
    .next_sim_amp = DEFAULT_SIM_AMP,
    .active_sim_amp = DEFAULT_SIM_AMP,
    .coeff_load_pending = false,
};

static void print_active_coeff(filter_type_e t)
{
    switch (t)
    {
    case _IIR_FLOAT:
    {
        df_iir_float_t *f = &s_filter.state.iir_f;
        ESP_LOGI(__func__, "iirf.Nb=%d iirf.Na=%d", f->Nb, f->Na);
        for (int i = 0; i < f->Nb; i++)
            ESP_LOGI(__func__, "b[%d]=%f", i, f->b[i]);
        for (int i = 0; i < f->Na; i++)
            ESP_LOGI(__func__, "a[%d]=%f", i, f->a[i]);
        break;
    }
    case _IIR_INT:
    {
        df_iir_int_t *f = &s_filter.state.iir_i;
        ESP_LOGI(__func__, "iiri.Nb=%d iiri.Na=%d", f->Nb, f->Na);
        for (int i = 0; i < f->Nb; i++)
            ESP_LOGI(__func__, "Sb[%d]=%ld", i, (long)f->Sb[i]);
        for (int i = 0; i < f->Na; i++)
            ESP_LOGI(__func__, "Sa[%d]=%ld", i, (long)f->Sa[i]);
        break;
    }
    case _FIR_FLOAT:
    {
        df_fir_float_t *f = &s_filter.state.fir_f;
        ESP_LOGI(__func__, "firf.M=%d", f->M);
        for (int i = 0; i < f->M; i++)
            ESP_LOGI(__func__, "b[%d]=%f", i, f->b[i]);
        break;
    }
    case _FIR_INT:
    {
        df_fir_int_t *f = &s_filter.state.fir_i;
        ESP_LOGI(__func__, "firi.M=%d", f->M);
        for (int i = 0; i < f->M; i++)
            ESP_LOGI(__func__, "Sb[%d]=%d", i, f->Sb[i]);
        break;
    }
    case _SOS_FLOAT:
    {
        df_sos_float_t *f = &s_filter.state.sos_f;
        ESP_LOGI(__func__, "sosf.N=%d", f->Nsos);
        for (int i = 0; i < f->Nsos; i++)
        {
            df_biquad_float_t *s = &f->stage[i];
            ESP_LOGI(__func__, "sosf%d: b=%f %f %f a=1 %f %f", i, s->b[0], s->b[1], s->b[2], s->a[1], s->a[2]);
        }
        break;
    }
    case _SOS_INT:
    {
        df_sos_int_t *f = &s_filter.state.sos_i;
        ESP_LOGI(__func__, "sosi.N=%d", f->Nsos);
        for (int i = 0; i < f->Nsos; i++)
        {
            df_biquad_int_t *s = &f->stage[i];
            ESP_LOGI(__func__, "sosi%d: Sb=%ld %ld %ld Sa=%ld %ld %ld", i,
                     (long)s->Sb[0], (long)s->Sb[1], (long)s->Sb[2],
                     (long)s->Sa[0], (long)s->Sa[1], (long)s->Sa[2]);
        }
        break;
    }
    case _LTC_FLOAT:
    {
        df_ltc_float_t *f = &s_filter.state.ltc_f;
        ESP_LOGI(__func__, "ltcf.Nnu=%d ltcf.Nk=%d", f->Nnu, f->Nk);
        for (int i = 0; i < f->Nnu; i++)
            ESP_LOGI(__func__, "nu[%d]=%f", i, f->nu[i]);
        for (int i = 0; i < f->Nk; i++)
            ESP_LOGI(__func__, "k[%d]=%f", i, f->k[i]);
        break;
    }
    case _LTC_INT:
    {
        df_ltc_int_t *f = &s_filter.state.ltc_i;
        ESP_LOGI(__func__, "ltci.Nnu=%d ltci.Nk=%d", f->Nnu, f->Nk);
        for (int i = 0; i < f->Nnu; i++)
            ESP_LOGI(__func__, "Snu[%d]=%ld", i, (long)f->Snu[i]);
        for (int i = 0; i < f->Nk; i++)
            ESP_LOGI(__func__, "Sk[%d]=%ld", i, (long)f->Sk[i]);
        break;
    }
    default:
        break;
    }
}

static inline void fts_cfg_set_sim_mode(fgen_sim_mode_e m)
{
    g_fts_cfg.next_sim_mode = m;
    g_fts_cfg.sim_mode_pending = true;
}

static inline void fts_cfg_set_sim_amp(int16_t a)
{
    g_fts_cfg.next_sim_amp = a;
    g_fts_cfg.sim_amp_pending = true;
    ESP_LOGI(__func__, "%d", a);
}

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
        fgen_sim_set_freq(&s_fgen, g_fts_cfg.active_sim_freq_hz);
        ESP_LOGI(__func__, "SIM freq -> %.1f Hz", (double)g_fts_cfg.active_sim_freq_hz);
    }

    if (g_fts_cfg.sim_amp_pending)
    {
        g_fts_cfg.sim_amp_pending = false;
        g_fts_cfg.active_sim_amp = g_fts_cfg.next_sim_amp;
        fgen_sim_set_amp(&s_fgen, g_fts_cfg.active_sim_amp);
    }

    if (g_fts_cfg.sim_mode_pending)
    {
        g_fts_cfg.sim_mode_pending = false;
        g_fts_cfg.active_sim_mode = g_fts_cfg.next_sim_mode;
        fgen_sim_set_mode(&s_fgen, g_fts_cfg.active_sim_mode);
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
        ESP_LOGI(__func__, "Input -> %s", g_fts_cfg.active_input == FTS_INPUT_ADC ? "ADC" : "SIM");
    }

    if (reinit_filter)
    {
        df_init(&s_filter,
                g_fts_cfg.active_filter,
                filter_get_coeff(g_fts_cfg.active_filter),
                DEFAULT_DIRECT_FORM,
                DEFAULT_QUANT_MODE);
        g_coeff_override = NULL;
        ESP_LOGI(__func__, "Filter reinit -> type %d", (int)g_fts_cfg.active_filter);
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

static fts_input_mode_e str_to_imode(const char *s)
{
    return (!strcmp(s, "gpio")) ? FTS_INPUT_ADC : FTS_INPUT_SIM;
}
static int fmode_str_to_enum(const char *s)
{
    static const char *names[] = {"iiri", "iirf", "firi", "firf", "sosi", "sosf", "ltci", "ltcf"};
    static const filter_type_e vals[] = {_IIR_INT, _IIR_FLOAT, _FIR_INT, _FIR_FLOAT, _SOS_INT, _SOS_FLOAT, _LTC_INT, _LTC_FLOAT};
    for (int i = 0; i < 8; i++)
        if (!strcmp(s, names[i]))
            return vals[i];
    return -1;
}

static void do_reset(void)
{
    gptimer_stop(gptimer);
    g_fts_cfg.timer_state = FTS_TIMER_STOPPED;
    g_fts_cfg.next_filter = DEFAULT_FILTER_TYPE;
    g_fts_cfg.filter_pending = true;
    g_fts_cfg.next_input = DEFAULT_INPUT_MODE;
    g_fts_cfg.input_pending = true;
    g_fts_cfg.next_sim_freq_hz = DEFAULT_SIM_FREQ_HZ;
    g_fts_cfg.sim_freq_pending = true;
    g_fts_cfg.next_sim_amp = DEFAULT_SIM_AMP;
    g_fts_cfg.sim_amp_pending = true;
    g_fts_cfg.next_sim_mode = DEFAULT_SIM_MODE;
    g_fts_cfg.sim_mode_pending = true;
    ESP_LOGI(__func__, "RESET: filter=%d input=%d freq=%.1f amp=%d mode=%d",
             (int)DEFAULT_FILTER_TYPE, (int)DEFAULT_INPUT_MODE,
             (double)DEFAULT_SIM_FREQ_HZ, (int)DEFAULT_SIM_AMP, (int)DEFAULT_SIM_MODE);
}

static void uart_cmd_process_line(char *line)
{
    // ESP_LOGI("DBGLINE", "raw len=%d line=[%s]", (int)strlen(line), line);

    char *end = line + strlen(line) - 1;
    while (end >= line && (*end == '\r' || *end == '\n'))
    {
        *end-- = '\0';
    }
    if (strlen(line) == 0)
    {
        return;
    }

    /* coeff-load passthrough mode */
    if (g_fts_cfg.coeff_load_pending)
    {
        ESP_LOGI("DBGLINE", "stripped len=%d line=[%s]", (int)strlen(line), line);
        coeff_loader_feed_line(line);
        coeff_load_state_e st = coeff_loader_state();
        if (st == CL_DONE_OK)
        {
            filter_type_e t;
            const void *c;
            if (coeff_loader_commit(&t, &c))
            {
                g_coeff_override = c;
                g_fts_cfg.next_filter = t;
                g_fts_cfg.filter_pending = true;
                ESP_LOGI(__func__, "COEFF: load OK, type=%d", (int)t);
                gptimer_start(gptimer);
                g_fts_cfg.timer_state = FTS_TIMER_RUNNING;
            }
            else
            {
                ESP_LOGW(__func__, "COEFF: validation FAILED, restoring defaults");
                /* full reset, same as "rest" command */
                do_reset();
            }
            g_fts_cfg.coeff_load_pending = false;
            return; /* skip the unconditional timer_pending block below */
        }
        return;
    }

    if (!strcmp(line, "reset"))
    {
        coeff_loader_abort();
        g_fts_cfg.coeff_load_pending = false;
        do_reset();
    }
    else if (!strcmp(line, "ar"))
    {
        esp_restart();
    }
    else if (!strcmp(line, "stop"))
    {
        gptimer_stop(gptimer);
        g_fts_cfg.timer_state = FTS_TIMER_STOPPED;
    }
    else if (!strcmp(line, "start"))
    {
        gptimer_start(gptimer);
        g_fts_cfg.timer_state = FTS_TIMER_RUNNING;
    }
    else if (!strncmp(line, "imode:", 6))
        fts_cfg_set_input(str_to_imode(line + 6));
    else if (!strncmp(line, "freq:", 5))
        fts_cfg_set_sim_freq(strtof(line + 5, NULL));
    else if (!strncmp(line, "amp:", 4))
        fts_cfg_set_sim_amp((int16_t)atoi(line + 4));
    else if (!strncmp(line, "fmode:", 6))
    {
        int t = fmode_str_to_enum(line + 6);
        if (t >= 0)
            fts_cfg_set_filter((filter_type_e)t);
        else
            ESP_LOGW(__func__, "CMD: Wrong fmode '%s'", line + 6);
    }
    else if (!strcmp(line, "gcoeff"))
    {
        print_active_coeff(g_fts_cfg.active_filter); /* implement: dump struct via ESP_LOGI */
    }
    else if (!strncmp(line, "scoeff:", 7))
    {
        int t = fmode_str_to_enum(line + 7);
        if (t >= 0)
        {
            gptimer_stop(gptimer);
            g_fts_cfg.timer_state = FTS_TIMER_STOPPED;

            coeff_loader_begin((filter_type_e)t);
            g_fts_cfg.coeff_load_pending = true;
            ESP_LOGI(__func__, "COEFF: waiting for utx, type=%d", t);
        }
        else
            ESP_LOGW(__func__, "CMD: Wrong fmode '%s'", line + 7);
    }
    else if (!strcmp(line, "wave:sin"))
    {
        fts_cfg_set_sim_mode(_FGEN_SIM_SIN);
    }
    else if (!strcmp(line, "wave:imp"))
    {
        fts_cfg_set_sim_mode(_FGEN_SIM_IMP);
    }
    else
    {
        ESP_LOGW(__func__, "CMD: unknown '%s'", line);
    }
}

// static void uart_cmd_rx_cb(uint8_t *data, uint16_t len)
// {
//     (void)len;
//     char *buf = (char *)data;

//     char *line = strtok(buf, "\n");
//     while (line != NULL)
//     {
//         uart_cmd_process_line(line);
//         line = strtok(NULL, "\n");
//     }
// }

#define RX_LINE_BUFFER_SIZE 4096

static char rx_accum[RX_LINE_BUFFER_SIZE];
static size_t rx_used = 0;

static void uart_cmd_rx_cb(uint8_t *data, uint16_t len)
{
    if (rx_used + len >= RX_LINE_BUFFER_SIZE)
    {
        ESP_LOGE("UART", "RX accumulation overflow");
        rx_used = 0;
        return;
    }

    memcpy(rx_accum + rx_used, data, len);
    rx_used += len;

    while (1)
    {
        char *newline = memchr(rx_accum, '\n', rx_used);

        if (!newline)
            break;

        size_t line_len = newline - rx_accum;

        *newline = '\0';

        uart_cmd_process_line(rx_accum);

        size_t remaining =
            rx_used - (line_len + 1);

        memmove(rx_accum,
                newline + 1,
                remaining);

        rx_used = remaining;
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

    // UDP Init

#if (ACTIVE_SEND_MODE == SEND_MODE_UDP)
    wifi_udp_handler_config_t wifi_udp_handler_config = {
        // .target_ip = "192.168.0.158",
        .target_ip = "10.17.36.16",
        .target_port = 2812,
    };
    wifi_udp_handler_init(&wifi_udp_handler_config);
#endif

#if (ACTIVE_SEND_MODE == SEND_MODE_USB)
    // USB CDC
    usb_cdc_driver_config_t usb_cfg = {
        .enable_port_0 = true,
        .enable_port_1 = false,
    };
    ESP_ERROR_CHECK(usb_cdc_driver_init(&usb_cfg));
#endif

    // ADC(always init so it is ready when "input adc" is commanded)
    adc_init();

    // UART command interface(uses your uart_driver component) * UART1, 115200 - 8N1, GPIO 17 TX / 18 RX  – adjust pins as needed

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
    ESP_LOGI(__func__, "UART command interface ready (UART%d, 115200-8N1)",
             s_uart_cmd.config.uart_port);

    // Digital filter(initial type and coefficients from filter_config.h)

    df_init(&s_filter,
            DEFAULT_FILTER_TYPE,
            filter_get_coeff(DEFAULT_FILTER_TYPE),
            DEFAULT_DIRECT_FORM,
            DEFAULT_QUANT_MODE);

    ESP_LOGI(__func__, "Boot: filter type=%d  input=%s  sim_freq=%.1f Hz",
             (int)DEFAULT_FILTER_TYPE,
             DEFAULT_INPUT_MODE == FTS_INPUT_ADC ? "ADC" : "SIM",
             (double)DEFAULT_SIM_FREQ_HZ);

    // GP timer(fires every Ts = 1 / Fs)

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

    // Sampling loop

#if (ENABLE_DEBUG_LOGS == 1)
    static int64_t last_t = 0;
    static int64_t period_sum = 0;
    static int64_t exec_sum = 0;
    static uint32_t cnt = 0;
#endif

    fgen_sim_init(&s_fgen, fs_Hz);
    fgen_sim_set_amp(&s_fgen, g_fts_cfg.active_sim_amp);
    fgen_sim_set_mode(&s_fgen, g_fts_cfg.active_sim_mode);
    fgen_sim_set_freq(&s_fgen, DEFAULT_SIM_FREQ_HZ);

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

        // INPUT ACQUISITION
        int16_t adc_raw = 0;

        if (g_fts_cfg.active_input == FTS_INPUT_ADC)
        {
            /* ---- Real ADC path  */
            int raw = 0;
            ESP_ERROR_CHECK(adc_oneshot_read(s_adc_handle, ADC_CHANNEL, &raw));
            /* raw is 0..4095 for 12-bit.  Centre around zero. */
            adc_raw = (int16_t)(raw - 2048);
        }
        else if (g_fts_cfg.active_input == FTS_INPUT_SIM)
        {
            adc_raw = fgen_sim_tic(&s_fgen, NULL);
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
        // s_dsm_pkt.aux0 = (g_fts_cfg.active_input == FTS_INPUT_SIM) ? phase_norm : 0.0f;
        // s_dsm_pkt.aux1 = (g_fts_cfg.active_input == FTS_INPUT_SIM) ? sine : 0.0f;
        // s_dsm_pkt.aux2 = (float)filt_y;
        // s_dsm_pkt.aux3 = (float)g_fts_cfg.active_filter; /* active filter ID  */
        // s_dsm_pkt.aux4 = (float)g_fts_cfg.active_input;  /* input source flag */
        // s_dsm_pkt.aux5 = g_fts_cfg.active_sim_freq_hz;   /* DDS frequency     */
        //                                                  /* aux6..aux15: reserved for future use */

        int64_t dt_us = esp_timer_get_time() - t1;
        s_dsm_pkt.dt = (int16_t)dt_us;
        s_dsm_pkt.crc16 = dsm_calc_xor16((const uint8_t *)&s_dsm_pkt.sig,
                                         DSM_PACKET_SIZE_BYTES - 2);

#if (ACTIVE_SEND_MODE == SEND_MODE_USB)
        //  TRANSMIT OVER USB CDC
        if (usb_cdc_is_connected(USB_CDC_PORT_0))
        {
            usb_cdc_write(USB_CDC_PORT_0,
                          (const uint8_t *)&s_dsm_pkt,
                          sizeof(s_dsm_pkt));
        }
#endif

#if (ACTIVE_SEND_MODE == SEND_MODE_UDP)

        //  BATCH INTO UDP BUFFER
        memcpy(s_batch_buf + sizeof(dsm_batch_hdr_t) + (s_batch_idx * sizeof(dsm_packet_t)),

               &s_dsm_pkt,

               sizeof(dsm_packet_t));

        s_batch_idx++;

        if (s_batch_idx == UDP_BATCH_SIZE)

        {

            dsm_batch_hdr_t hdr;

            hdr.batch_id = s_batch_id++;

            hdr.count = UDP_BATCH_SIZE;

            memcpy(s_batch_buf, &hdr, sizeof(hdr));

            wifi_udp_handler_send(s_batch_buf, sizeof(s_batch_buf));

            s_batch_idx = 0;
        }

#endif

        // DIAGNOSTICS  (compiled out when ENABLE_DEBUG_LOGS == 0)
#if (ENABLE_DEBUG_LOGS == 1)
        exec_sum += (esp_timer_get_time() - t1);
        cnt++;
        if (cnt >= 100)
        {
            ESP_LOGI(__func__,
                     "avg_period=%lld us  avg_exec=%lld us  filt=%d  input=%s",
                     period_sum / (cnt - 1),
                     exec_sum / cnt,
                     (int)g_fts_cfg.active_filter,
                     g_fts_cfg.active_input == FTS_INPUT_ADC ? "ADC" : "SIM");
            cnt = period_sum = exec_sum = 0;
        }
#endif
    }
}
