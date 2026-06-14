/**
 * @file    filter_config.h
 * @brief   Filter coefficient bank + runtime configuration for the FTS app.
 *
 * 
 * HOW TO USE
 * 
 *
 * At compile time
 * ---------------
 *  The DEFAULT_FILTER_TYPE macro selects the filter that starts running at
 *  boot.  Change it to any filter_type_e value and rebuild.
 *
 * At runtime (via UART command handler)
 * --------------------------------------
 *  Call  fts_cfg_set_filter(type)  from your UART command dispatcher.
 *  The sampling loop detects the pending change, re-initialises the
 *  df_handle safely between ticks, and resets the delay lines.
 *
 *  Call  fts_cfg_set_input(mode)   to toggle between simulation and ADC.
 *  Call  fts_cfg_set_sim_freq(hz)  to change the DDS tone frequency.
 *
 * 
 * COEFFICIENT BANK  (besselLP9 @ 555 Hz cutoff, Fs = 5 kHz)
 * 
 *  All integer coefficients are pre-scaled exactly as output by the tool
 *  (.utx files).  No further scaling is performed by the component.
 *
 *  IIR int Q:   2^20 = 1048576   (Sa[0] must equal 1048576)
 *  FIR int Q:   2^13 = 8192      (Sb[] direct values from utx)
 *  SOS int Q:   2^20 = 1048576   (each row: Sb0 Sb1 Sb2 Sa0 Sa1 Sa2)
 *  LTC int Q:   2^14 = 16384     (Sk[], Snu[] direct values from utx)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "digital_filters.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * Input source
 *  */

typedef enum {
    FTS_INPUT_SIM = 0,   /**< DDS sine-wave simulation             */
    FTS_INPUT_ADC = 1,   /**< Real ADC samples                     */
} fts_input_mode_e;

/* 
 * Runtime configuration state  (written by command handler, read by loop)
 *
 * Access pattern:
 *   UART task:     fts_cfg_set_*() functions
 *   Sampling loop: fts_cfg_apply_pending() once per tick (very cheap,
 *                  only does real work when a flag is set)
 *  */

typedef struct {
    /* Pending-change flags (set by command handler) */
    volatile bool        filter_pending;
    volatile bool        input_pending;
    volatile bool        sim_freq_pending;

    /* Desired values (written by command handler) */
    volatile filter_type_e    next_filter;
    volatile fts_input_mode_e next_input;
    volatile float            next_sim_freq_hz;

    /* Active values (read by sampling loop) */
    filter_type_e    active_filter;
    fts_input_mode_e active_input;
    float            active_sim_freq_hz;
} fts_config_t;

/* Single global instance – declared here, defined in main.c */
extern fts_config_t g_fts_cfg;

/* ---- Setters called from UART command handler (any task) -------------- */

static inline void fts_cfg_set_filter(filter_type_e t)
{
    g_fts_cfg.next_filter    = t;
    g_fts_cfg.filter_pending = true;
}

static inline void fts_cfg_set_input(fts_input_mode_e m)
{
    g_fts_cfg.next_input    = m;
    g_fts_cfg.input_pending = true;
}

static inline void fts_cfg_set_sim_freq(float hz)
{
    g_fts_cfg.next_sim_freq_hz  = hz;
    g_fts_cfg.sim_freq_pending  = true;
}

/* 
 * Default boot configuration
 *  */

#define DEFAULT_FILTER_TYPE     _IIR_FLOAT
#define DEFAULT_INPUT_MODE      FTS_INPUT_SIM
#define DEFAULT_SIM_FREQ_HZ     100.0f

/* IIR integer options (only affect _IIR_INT) */
#define DEFAULT_DIRECT_FORM     DF_DIRECT_FORM_1
#define DEFAULT_QUANT_MODE      DF_QUANT_ROUND

/* 
 * Coefficient bank – IIR float
 *   besselLP9_555_IIR_float.utx
 *  */
static const df_iir_float_coeff_t coeff_iir_float = {
    .b  = { 0.000852933555217f, 0.00767640199695f, 0.0307056079878f,
            0.0716464186382f,   0.107469627957f,   0.107469627957f,
            0.0716464186382f,   0.0307056079878f,  0.00767640199695f,
            0.000852933555217f },
    .a  = { 1.0f,            -1.51812930171f,   1.75535760798f,
           -1.30615492445f,   0.729096023248f,  -0.294232362116f,
            0.085905807656f, -0.0171237790051f,  0.00210303765231f,
           -0.000120128979675f },
    .Nb = 10,
    .Na = 10,
};

/* 
 * Coefficient bank – IIR int  (Q20, pre-scaled)
 *   besselLP9_555_IIR_int.utx
 *   Sa[0] = 1048576 = 2^20
 *  */
static const df_iir_int_coeff_t coeff_iir_int = {
    .Sb = { 894, 8049, 32197, 75127, 112690, 112690, 75127, 32197, 8049, 894 },
    .Sa = { 1048576, -1591874, 1840626, -1369603, 764513,
           -308525,    90079,   -17956,     2205,   -126 },
    .Nb = 10,
    .Na = 10,
};

/* 
 * Coefficient bank – FIR float  (three lengths available)
 *   besselLP9_555_FIR_21_float.utx  (default)
 *  */
static const df_fir_float_coeff_t coeff_fir_float = {
    .b = { 0.000852933555217f,  0.00897126541954f,  0.0428279454894f,
           0.122031061974f,     0.228646190953f,    0.290025935741f,
           0.241319611581f,     0.109481024075f,   -0.00522754130118f,
          -0.0380109122215f,   -0.0137816433297f,   0.00890319219537f,
           0.00785340094475f,  -0.00138649677679f, -0.00354621798539f,
          -0.000263759330188f,  0.00146438253225f,  0.000454298559491f,
          -0.000545539755738f, -0.000330843376568f, 0.000168908814841f },
    .M = 21,
};

/* FIR float – 20-tap variant  (besselLP9_555_FIR_20_float.utx) */
static const df_fir_float_coeff_t coeff_fir_float_20 = {
    .b = { 0.000912298193261f, 0.00953584404133f,  0.0451695054786f,
           0.127424732944f,    0.235603868803f,    0.293271149897f,
           0.236662739623f,    0.0998300703103f,  -0.0121370988282f,
          -0.0383558248494f,  -0.0108956735586f,   0.0102694305019f,
           0.00702207539064f, -0.00227306010971f, -0.0033959329324f,
           0.000201992147517f, 0.00150492310456f,  0.000233734646266f,
          -0.000618004187239f,-0.000237710867692f },
    .M = 20,
};

/* FIR float – 23-tap variant  (besselLP9_555_FIR_23_float.utx) */
static const df_fir_float_coeff_t coeff_fir_float_23 = {
    .b = { 0.000852933555217f,  0.00897126541954f,  0.0428279454894f,
           0.122031061974f,     0.228646190953f,    0.290025935741f,
           0.241319611581f,     0.109481024075f,   -0.00522754130118f,
          -0.0380109122215f,   -0.0137816433297f,   0.00890319219537f,
           0.00785340094475f,  -0.00138649677679f, -0.00354621798539f,
          -0.000263759330188f,  0.00146438253225f,  0.000454298559491f,
          -0.000545539755738f, -0.000330843376568f, 0.000168908814841f,
           0.000190048960376f, -3.17132276354e-05f },
    .M = 23,
};

/* 
 * Coefficient bank – FIR int  (Q13, pre-scaled)
 *   besselLP9_555_FIR_21_int.utx  (default)
 *  */
static const df_fir_int_coeff_t coeff_fir_int = {
    .Sb = { 7, 73, 351, 1000, 1873, 2376, 1977, 897, -43, -311,
           -113, 73, 64, -11, -29, -2, 12, 4, -4, -3, 1 },
    .M  = 21,
};

/* FIR int – 20-tap variant  (besselLP9_555_FIR_20_int.utx) */
static const df_fir_int_coeff_t coeff_fir_int_20 = {
    .Sb = { 7, 78, 370, 1044, 1930, 2402, 1939, 818, -99, -314,
           -89, 84, 58, -19, -28, 2, 12, 2, -5, -2 },
    .M  = 20,
};

/* FIR int – 23-tap variant  (besselLP9_555_FIR_23_int.utx) */
static const df_fir_int_coeff_t coeff_fir_int_23 = {
    .Sb = { 7, 73, 351, 1000, 1873, 2376, 1977, 897, -43, -311,
           -113, 73, 64, -11, -29, -2, 12, 4, -4, -3, 1, 2, 0 },
    .M  = 23,
};

/* 
 * Coefficient bank – SOS float
 *   besselLP9_555_sos_float.utx
 *   Row format: [ b0  b1  b2  a0(=1)  a1  a2 ]
 *  */
static const df_sos_float_coeff_t coeff_sos_float = {
    .sos = {
        { 0.243322931475f, 0.466162375192f, 0.22331474186f,  1.0f, -0.24245244434f,  0.456702095896f },
        { 0.243322931475f, 0.473694118923f, 0.230901384066f, 1.0f, -0.33203214577f,  0.22492322465f  },
        { 0.243322931475f, 0.488897241239f, 0.246170221857f, 1.0f, -0.367561721959f, 0.110788880202f },
        { 0.243322931475f, 0.505075906794f, 0.262401080697f, 1.0f, -0.382638061655f, 0.0545666085897f},
        { 0.243322931475f, 0.256076741132f, 0.0f,            1.0f, -0.193444927988f, 0.0f            },
    },
    .Nsos = 5,
};

/* 
 * Coefficient bank – SOS int  (Q20, pre-scaled)
 *   besselLP9_555_sos_int.utx
 *   Row format: [ Sb0  Sb1  Sb2  Sa0(=1048576)  Sa1  Sa2 ]
 *  */
static const df_sos_int_coeff_t coeff_sos_int = {
    .sos = {
        { 255143, 488807, 234162, 1048576, -254230,  478887 },
        { 255143, 496704, 242118, 1048576, -348161,  235849 },
        { 255143, 512646, 258128, 1048576, -385416,  116171 },
        { 255143, 529610, 275147, 1048576, -401225,   57217 },
        { 255143, 268516,      0, 1048576, -202842,       0 },
    },
    .Nsos = 5,
};

/* 
 * Coefficient bank – LTC float
 *   besselLP9_555_LTC_float.utx
 *  */
static const df_ltc_float_coeff_t coeff_ltc_float = {
    .nu  = { -0.01366277258327422f, -0.01651811304526455f,  0.07474913864231153f,
              0.2250065517632938f,   0.2876764697249627f,   0.2285434841039772f,
              0.12202991579742f,     0.04282794341947878f,  0.008971265419540488f,
              0.0008529335552171375f },
    .Nnu = 10,
    .k   = { -0.5512823103959078f,  0.6787790885725167f, -0.5040706107711028f,
              0.3518170320817122f, -0.1750105815454953f,  0.06114063990974815f,
             -0.01399714213495528f, 0.00192066635599589f,-0.0001201289796754797f },
    .Nk  = 9,
};

/* 
 * Coefficient bank – LTC int  (Q14, pre-scaled)
 *   besselLP9_555_LTC_int.utx
 *  */
static const df_ltc_int_coeff_t coeff_ltc_int = {
    .Snu = { -224, -271, 1225, 3687, 4713, 3744, 1999, 702, 147, 14 },
    .Nnu = 10,
    .Sk  = { -9032, 11121, -8259, 5764, -2867, 1002, -229, 31, -2 },
    .Nk  = 9,
};

/* 
 * Coefficient selector  –  maps filter_type_e → const void *
 *  */
static inline const void *filter_get_coeff(filter_type_e t)
{
    switch (t) {
    case _IIR_FLOAT: return &coeff_iir_float;
    case _IIR_INT:   return &coeff_iir_int;
    case _FIR_FLOAT: return &coeff_fir_float;
    case _FIR_INT:   return &coeff_fir_int;
    case _SOS_FLOAT: return &coeff_sos_float;
    case _SOS_INT:   return &coeff_sos_int;
    case _LTC_FLOAT: return &coeff_ltc_float;
    case _LTC_INT:   return &coeff_ltc_int;
    default:         return &coeff_iir_float;
    }
}

#ifdef __cplusplus
}
#endif