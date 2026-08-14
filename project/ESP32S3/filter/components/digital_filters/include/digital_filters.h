#ifndef DIGITAL_FILTERS_H
#define DIGITAL_FILTERS_H

/**
 * @file    digital_filters.h
 * @brief   Public API – digital_filters ESP-IDF component  (v2)
 *
 * Key change from v1
 * ------------------
 * Integer filter types (_IIR_INT, _FIR_INT, _SOS_INT, _LTC_INT)  accepts
 * PRE-SCALED integer coefficient structs (df_*_int_coeff_t) whose members are
 * already the exact Q-format integers produced by the filter-design tool (e.g.
 * besselLP9_555_IIR_int.utx).  No floating-point scaling is performed inside
 * the component for integer types.
 *
 * Float types (_IIR_FLOAT, _FIR_FLOAT, _SOS_FLOAT, _LTC_FLOAT) continue to
 * accept df_*_float_coeff_t structs whose members are IEEE-754 floats.
 *
 * Coefficient struct summary
 * --------------------------
 *  _IIR_FLOAT  →  df_iir_float_coeff_t  { float b[], float a[], Nb, Na }
 *  _IIR_INT    →  df_iir_int_coeff_t    { int32_t Sb[], int32_t Sa[], Nb, Na }
 *                   Sa[0] must equal DF_S_IIR (= 2^20 = 1048576) i.e. a0=1 scaled
 *  _FIR_FLOAT  →  df_fir_float_coeff_t  { float b[], M }
 *  _FIR_INT    →  df_fir_int_coeff_t    { int16_t Sb[], M }
 *                   values are Q13 (scale = 2^13 = 8192)
 *  _SOS_FLOAT  →  df_sos_float_coeff_t  { float sos[][6], Nsos }
 *                   row = [ b0 b1 b2 a0(=1) a1 a2 ]
 *  _SOS_INT    →  df_sos_int_coeff_t    { int32_t sos[][6], Nsos }
 *                   row = [ Sb0 Sb1 Sb2 Sa0(=2^20) Sa1 Sa2 ] (Q20)
 *  _LTC_FLOAT  →  df_ltc_float_coeff_t  { float nu[], float k[], Nnu, Nk }
 *  _LTC_INT    →  df_ltc_int_coeff_t    { int32_t Snu[], int32_t Sk[], Nnu, Nk }
 *                   Q14 (scale = 2^14 = 16384)
 *
 * Runtime reconfiguration
 * -----------------------
 * df_init() may be called at any time (from the same task that owns the
 * handle) to switch filter type or update coefficients.  It resets all
 * internal state.  Use df_reset() to clear state without changing type/coeff.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "df_dsp_utils.h"
#include "df_xbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===
 * Enumerations
 *  */

typedef enum {
    _IIR_INT   = 0,
    _IIR_FLOAT = 1,
    _FIR_INT   = 2,
    _FIR_FLOAT = 3,
    _SOS_INT   = 4,
    _SOS_FLOAT = 5,
    _LTC_INT   = 6,
    _LTC_FLOAT = 7,
    _FILTER_TYPE_COUNT
} filter_type_e;

typedef enum {
    DF_QUANT_ROUND = 0,
    DF_QUANT_FLOOR = 1,
    DF_QUANT_FIX   = 2,
} df_quant_mode_e;

typedef enum {
    DF_DIRECT_FORM_1 = 1,
    DF_DIRECT_FORM_2 = 2,
} df_direct_form_e;

/* ===
 * Coefficient descriptors  –  FLOAT types
 *  */

typedef struct {
    float   b[DF_MAX_IIR_TAPS];
    float   a[DF_MAX_IIR_TAPS];  /**< a[0] must be 1.0                       */
    uint8_t Nb;
    uint8_t Na;
} df_iir_float_coeff_t;

typedef struct {
    float    b[DF_MAX_FIR_TAPS];
    uint16_t M;
} df_fir_float_coeff_t;

typedef struct {
    float   sos[DF_MAX_SOS_STAGES][6]; /**< [b0 b1 b2 a0(=1) a1 a2] per row  */
    uint8_t Nsos;
} df_sos_float_coeff_t;

typedef struct {
    float   nu[DF_MAX_LTC_ORDER + 1];
    float   k [DF_MAX_LTC_ORDER + 1];
    uint8_t Nnu;
    uint8_t Nk;
} df_ltc_float_coeff_t;

/* ===
 * Coefficient descriptors  –  INT types  (pre-scaled, tool-generated)
 *  */

/**
 * IIR integer coefficients  (Q20, scale = 2^20 = 1048576)
 *  Sb[] = numerator  scaled integers  (from utx: iiri.Sb)
 *  Sa[] = denominator scaled integers (from utx: iiri.Sa),  Sa[0] = 1048576
 */
typedef struct {
    int32_t Sb[DF_MAX_IIR_TAPS];
    int32_t Sa[DF_MAX_IIR_TAPS];
    uint8_t Nb;
    uint8_t Na;
} df_iir_int_coeff_t;

/**
 * FIR integer coefficients  (Q13, scale = 2^13 = 8192)
 *  Sb[] = pre-scaled tap values  (from utx: firi.Sb)
 */
typedef struct {
    int16_t  Sb[DF_MAX_FIR_TAPS];
    uint16_t M;
} df_fir_int_coeff_t;

/**
 * SOS integer coefficients  (Q20, scale = 2^20 = 1048576)
 *  sos[][6] = [ Sb0 Sb1 Sb2 Sa0(=1048576) Sa1 Sa2 ] per row
 *              (from utx: sosi0, sosi1, …)
 */
typedef struct {
    int32_t sos[DF_MAX_SOS_STAGES][6];
    uint8_t Nsos;
} df_sos_int_coeff_t;

/**
 * LTC integer coefficients  (Q14, scale = 2^14 = 16384)
 *  Snu[] = pre-scaled ladder gains  (from utx: ltci.Snu)
 *  Sk[]  = pre-scaled reflection    (from utx: ltci.Sk)
 */
typedef struct {
    int32_t Snu[DF_MAX_LTC_ORDER + 1];
    int32_t Sk [DF_MAX_LTC_ORDER + 1];
    uint8_t Nnu;
    uint8_t Nk;
} df_ltc_int_coeff_t;

/* ===
 * Concrete filter state structures
 *  */

/* ----- IIR float -------------------------------------------------------- */
typedef struct {
    float   b[DF_MAX_IIR_TAPS];
    float   a[DF_MAX_IIR_TAPS];
    float   ybuf[DF_MAX_IIR_TAPS];
    uint8_t Nb, Na, nybuf;
} df_iir_float_t;

void    df_iir_float_init  (df_iir_float_t *f, const df_iir_float_coeff_t *c);
float   df_iir_float_tick  (df_iir_float_t *f, const df_xbuf_t *xb);
int16_t df_iir_float_tick16(df_iir_float_t *f, const df_xbuf_t *xb);

/* ----- IIR integer (Q20) ------------------------------------------------ */
typedef struct {
    int32_t Sb[DF_MAX_IIR_TAPS];  /**< Numerator   (pre-scaled, copied as-is) */
    int32_t Sa[DF_MAX_IIR_TAPS];  /**< Denominator (pre-scaled, copied as-is) */
    int32_t ybuf[DF_MAX_IIR_TAPS];
    uint8_t nybuf;
    int64_t wbuf[DF_MAX_IIR_TAPS];
    uint8_t nwbuf;
    uint8_t Nb, Na;
    df_direct_form_e direct_form;
    df_quant_mode_e  quant_mode;
    uint32_t         n_sat;
} df_iir_int_t;

void    df_iir_int_init(df_iir_int_t *f, const df_iir_int_coeff_t *c,
                        df_direct_form_e form, df_quant_mode_e qm);
int16_t df_iir_int_tick(df_iir_int_t *f, const df_xbuf_t *xb);

/* ----- FIR float -------------------------------------------------------- */
typedef struct {
    float    b[DF_MAX_FIR_TAPS];
    uint16_t M, L;
    bool     sym_plus, sym_minus;
} df_fir_float_t;

void    df_fir_float_init  (df_fir_float_t *f, const df_fir_float_coeff_t *c);
float   df_fir_float_tick  (df_fir_float_t *f, const df_xbuf_t *xb);
int16_t df_fir_float_tick16(df_fir_float_t *f, const df_xbuf_t *xb);

/* ----- FIR integer (Q13) ------------------------------------------------ */
typedef struct {
    int16_t  Sb[DF_MAX_FIR_TAPS]; /**< Pre-scaled taps (copied as-is)        */
    uint16_t M, L;
    bool     sym_plus, sym_minus;
} df_fir_int_t;

void    df_fir_int_init(df_fir_int_t *f, const df_fir_int_coeff_t *c);
int16_t df_fir_int_tick(df_fir_int_t *f, const df_xbuf_t *xb);

/* ----- Biquad float ----------------------------------------------------- */
typedef struct {
    float b[3], a[3];
    float x1, x2, y1, y2;
} df_biquad_float_t;

void  df_biquad_float_init(df_biquad_float_t *s,
                            float b0, float b1, float b2,
                            float a0, float a1, float a2);
float df_biquad_float_tick(df_biquad_float_t *s, float x);

/* ----- Biquad integer --------------------------------------------------- */
typedef struct {
    int32_t Sb[3], Sa[3];  /**< Pre-scaled (Q20), copied as-is               */
    int16_t x1, x2;
    int64_t y1, y2;
} df_biquad_int_t;

void    df_biquad_int_init(df_biquad_int_t *s,
                            int32_t Sb0, int32_t Sb1, int32_t Sb2,
                            int32_t Sa0, int32_t Sa1, int32_t Sa2);
int16_t df_biquad_int_tick(df_biquad_int_t *s, int16_t x);

/* ----- SOS float -------------------------------------------------------- */
typedef struct {
    df_biquad_float_t stage[DF_MAX_SOS_STAGES];
    uint8_t Nsos;
} df_sos_float_t;

void    df_sos_float_init  (df_sos_float_t *f, const df_sos_float_coeff_t *c);
float   df_sos_float_tick  (df_sos_float_t *f, float x);
int16_t df_sos_float_tick16(df_sos_float_t *f, int16_t x);

/* ----- SOS integer ------------------------------------------------------ */
typedef struct {
    df_biquad_int_t stage[DF_MAX_SOS_STAGES];
    uint8_t Nsos;
} df_sos_int_t;

void    df_sos_int_init(df_sos_int_t *f, const df_sos_int_coeff_t *c);
int16_t df_sos_int_tick(df_sos_int_t *f, int16_t x);

/* ----- LTC float -------------------------------------------------------- */
typedef struct {
    float   k  [DF_MAX_LTC_ORDER + 1];
    float   nu [DF_MAX_LTC_ORDER + 1];
    float   ubuf[DF_MAX_LTC_ORDER + 1];
    float   vbuf[DF_MAX_LTC_ORDER + 1];
    float   wbuf[DF_MAX_LTC_ORDER + 1];
    uint8_t Nk, Nnu;
} df_ltc_float_t;

void    df_ltc_float_init  (df_ltc_float_t *f, const df_ltc_float_coeff_t *c);
float   df_ltc_float_tick  (df_ltc_float_t *f, int16_t x);
int16_t df_ltc_float_tick16(df_ltc_float_t *f, int16_t x);

/* ----- LTC integer (Q14) ------------------------------------------------ */
typedef struct {
    int32_t Sk  [DF_MAX_LTC_ORDER + 1]; /**< Pre-scaled (Q14), copied as-is  */
    int32_t Snu [DF_MAX_LTC_ORDER + 1];
    int32_t ubuf[DF_MAX_LTC_ORDER + 1];
    int32_t vbuf[DF_MAX_LTC_ORDER + 1];
    int32_t wbuf[DF_MAX_LTC_ORDER + 1];
    uint8_t Nk, Nnu;
} df_ltc_int_t;

void    df_ltc_int_init(df_ltc_int_t *f, const df_ltc_int_coeff_t *c);
int16_t df_ltc_int_tick(df_ltc_int_t *f, int16_t x);

/* ===
 * Generic type-erased handle
 *  */

typedef struct {
    filter_type_e type;
    df_xbuf_t     xbuf;
    union {
        df_iir_float_t iir_f;
        df_iir_int_t   iir_i;
        df_fir_float_t fir_f;
        df_fir_int_t   fir_i;
        df_sos_float_t sos_f;
        df_sos_int_t   sos_i;
        df_ltc_float_t ltc_f;
        df_ltc_int_t   ltc_i;
    } state;
} df_handle_t;

/**
 * @brief Initialise (or reinitialise) a filter handle.
 *
 * May be called at runtime to switch type or coefficients.
 * Always resets internal delay-line state.
 *
 * @param h      Handle.
 * @param type   Filter type enum.
 * @param coeff  Pointer to the matching coeff struct (see table in file header).
 * @param form   DF_DIRECT_FORM_1 or _2  (IIR_INT only; ignored otherwise).
 * @param qm     Quantisation mode        (IIR_INT only; ignored otherwise).
 */
void df_init(df_handle_t *h, filter_type_e type, const void *coeff,
             df_direct_form_e form, df_quant_mode_e qm);

/** Process one sample; returns filtered 16-bit output. */
int16_t df_process(df_handle_t *h, int16_t x);

/** Clear delay lines; preserve type and coefficients. */
void df_reset(df_handle_t *h);

#ifdef __cplusplus
}
#endif

#endif // DIGITAL_FILTERS_H