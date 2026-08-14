/**
 * @file    digital_filters.c
 * @brief   digital_filters component – implementation  (v2)
 *
 * Changes from v1
 * ---------------
 *  • Integer types accept pre-scaled coefficients directly from the
 *    filter-design tool (.utx files).  No float→int conversion at init.
 *  • df_iir_int_coeff_t / df_fir_int_coeff_t / df_sos_int_coeff_t /
 *    df_ltc_int_coeff_t all hold the exact integer values from the utx.
 *  • df_biquad_int_init() now takes int32_t arguments (not floats).
 *  • df_fir_float_coeff_t / df_sos_float_coeff_t / df_ltc_float_coeff_t
 *    are now separate types from their integer counterparts.
 *  • Symmetry detection for FIR int works on the pre-scaled Sb[] integers.
 */

#include "digital_filters.h"
#include <string.h>
#include <math.h>

/* ===
 * Internal helpers
 *  */

static inline int64_t _qssat(int64_t x, uint32_t limit_ld, uint32_t lds,
                               df_quant_mode_e qm, uint32_t *n_sat)
{
    const int64_t half = (lds > 0) ? ((int64_t)1 << (lds - 1)) : 0;
    const int64_t lim  = limit_ld  ? ((int64_t)1 << limit_ld)  : 0;

    switch (qm) {
    case DF_QUANT_ROUND:
        x = (x >= 0) ? (x + half) : (x - half);
        break;
    case DF_QUANT_FLOOR:
        break;
    case DF_QUANT_FIX:
        if (x < 0) { x = -x; x >>= lds; x = -x; }
        else          x >>= lds;
        return x;
    }
    if (lim) {
        if      (x >  lim) { x =  lim; if (n_sat && *n_sat < UINT32_MAX) (*n_sat)++; }
        else if (x < -lim) { x = -lim; if (n_sat && *n_sat < UINT32_MAX) (*n_sat)++; }
    }
    return x >> lds;
}

/* ===
 * IIR – floating-point
 *  */

void df_iir_float_init(df_iir_float_t *f, const df_iir_float_coeff_t *c)
{
    memset(f, 0, sizeof(*f));
    f->Nb = DF_MIN(c->Nb, DF_MAX_IIR_TAPS);
    f->Na = DF_MIN(c->Na, DF_MAX_IIR_TAPS);
    for (uint8_t i = 0; i < f->Nb; i++) f->b[i] = c->b[i];
    for (uint8_t i = 0; i < f->Na; i++) f->a[i] = c->a[i];
}

float df_iir_float_tick(df_iir_float_t *f, const df_xbuf_t *xb)
{
    float yrec = 0.0f, ynrec = 0.0f;
    for (uint8_t i = 1; i < f->Na; i++) {
        int16_t ind = (int16_t)f->nybuf - i;
        if (ind < 0) ind += DF_MAX_IIR_TAPS;
        yrec += f->ybuf[(uint8_t)ind] * f->a[i];
    }
    for (uint8_t i = 0; i < f->Nb; i++)
        ynrec += (float)df_xbuf_get(xb, i) * f->b[i];
    float y = ynrec - yrec;
    f->ybuf[f->nybuf] = y;
    if (++f->nybuf >= DF_MAX_IIR_TAPS) f->nybuf = 0;
    return y;
}

int16_t df_iir_float_tick16(df_iir_float_t *f, const df_xbuf_t *xb)
{
    return (int16_t)DF_ROUND_F(df_iir_float_tick(f, xb));
}

/* ===
 * IIR – integer (Q20, pre-scaled coefficients)
 *
 * Sa[0] from the utx = 1048576 (= 2^20 = DF_S_IIR).
 * The multiply-accumulate mixes x[] (int16) with Sb[] (int32 Q20), and
 * y[] (int32, already in output-sample units * 2^LDY_IIR) with Sa[] (int32 Q20).
 * Two-stage scaling keeps intermediate values in 64-bit range.
 *  */

void df_iir_int_init(df_iir_int_t *f, const df_iir_int_coeff_t *c,
                     df_direct_form_e form, df_quant_mode_e qm)
{
    memset(f, 0, sizeof(*f));
    f->Nb          = DF_MIN(c->Nb, DF_MAX_IIR_TAPS);
    f->Na          = DF_MIN(c->Na, DF_MAX_IIR_TAPS);
    f->direct_form = form;
    f->quant_mode  = qm;
    /* Copy pre-scaled coefficients verbatim – no float conversion */
    for (uint8_t i = 0; i < f->Nb; i++) f->Sb[i] = c->Sb[i];
    for (uint8_t i = 0; i < f->Na; i++) f->Sa[i] = c->Sa[i];
}

int16_t df_iir_int_tick(df_iir_int_t *f, const df_xbuf_t *xb)
{
    if (f->direct_form == DF_DIRECT_FORM_1) {
        /* --- Direct Form I --- */
        /* Feedback: Σ Sa[i]*y[n-i]  for i=1..Na-1  (Sa in Q20, y in Q_LDY) */
        int64_t yrec = 0;
        for (uint8_t i = 1; i < f->Na; i++) {
            int16_t ind = (int16_t)f->nybuf - i;
            if (ind < 0) ind += DF_MAX_IIR_TAPS;
            yrec = df_madd64(yrec, f->ybuf[(uint8_t)ind], f->Sa[i]);
        }
        /* yrec is in [Q20 * Q_LDY]; bring back to Q_LDY scale */
        yrec = DF_ROUNDOFF(yrec, DF_R_IIR) >> DF_LDS_IIR;

        /* Feedforward: Σ Sb[i]*x[n-i]  (Sb in Q20, x in int16) → Q20 scale */
        int64_t ynrec = 0;
        for (uint8_t i = 0; i < f->Nb; i++)
            ynrec = df_madd64(ynrec, (int32_t)df_xbuf_get(xb, i), f->Sb[i]);
        /* Bring ynrec to Q_LDY scale (keep LDY extra bits for y*a products) */
        ynrec = DF_ROUNDOFF(ynrec, (int64_t)1 << (DF_LDS_IIR - DF_LDY_IIR - 1))
                >> (DF_LDS_IIR - DF_LDY_IIR);

        int64_t acc = ynrec - yrec;
        f->ybuf[f->nybuf] = (int32_t)acc;
        if (++f->nybuf >= DF_MAX_IIR_TAPS) f->nybuf = 0;
        return (int16_t)(DF_ROUNDOFF(acc, (int64_t)1 << (DF_LDY_IIR - 1))
                         >> DF_LDY_IIR);

    } else {
        /* --- Direct Form II --- */
        int64_t yrec = ((int64_t)df_xbuf_get(xb, 0)) << (2 * DF_LDS_IIR);
        for (uint8_t i = 1; i < f->Na; i++) {
            int16_t ind = (int16_t)f->nwbuf - i;
            if (ind < 0) ind += DF_MAX_IIR_TAPS;
            if (f->Sa[i]) yrec -= (int64_t)f->wbuf[(uint8_t)ind] * f->Sa[i];
        }
        int64_t acc = _qssat(yrec, (uint32_t)(2 * DF_LDS_IIR + 16),
                             DF_LDS_IIR, f->quant_mode, &f->n_sat);
        f->wbuf[f->nwbuf] = acc;

        int64_t ynrec = 0;
        for (uint8_t i = 0; i < f->Nb; i++) {
            int16_t ind = (int16_t)f->nwbuf - i;
            if (ind < 0) ind += DF_MAX_IIR_TAPS;
            if (f->Sb[i]) ynrec += (int64_t)f->wbuf[(uint8_t)ind] * f->Sb[i];
        }
        if (++f->nwbuf >= DF_MAX_IIR_TAPS) f->nwbuf = 0;
        return (int16_t)_qssat(ynrec, (uint32_t)(2 * DF_LDS_IIR + 16),
                                (uint32_t)(2 * DF_LDS_IIR),
                                f->quant_mode, &f->n_sat);
    }
}

/* ===
 * FIR – floating-point
 *  */

void df_fir_float_init(df_fir_float_t *f, const df_fir_float_coeff_t *c)
{
    memset(f, 0, sizeof(*f));
    f->M = (uint16_t)DF_MIN(c->M, DF_MAX_FIR_TAPS);
    f->L = f->M >> 1;
    for (uint16_t i = 0; i < f->M; i++) f->b[i] = c->b[i];

    f->sym_plus = f->sym_minus = true;
    const float eps = 1e-7f;
    for (uint16_t i = 0; i < f->L; i++) {
        if (fabsf(f->b[i] - f->b[f->M-1-i]) > eps) f->sym_plus  = false;
        if (fabsf(f->b[i] + f->b[f->M-1-i]) > eps) f->sym_minus = false;
    }
}

float df_fir_float_tick(df_fir_float_t *f, const df_xbuf_t *xb)
{
    float y = 0.0f;
    const uint16_t M = f->M, L = f->L;

    if (f->sym_plus) {
        if (M & 1u) y = (float)df_xbuf_get(xb, L) * f->b[L];
        for (uint16_t i = 0; i < L; i++)
            y += ((float)df_xbuf_get(xb, i) + (float)df_xbuf_get(xb, M-1u-i)) * f->b[i];
    } else if (f->sym_minus) {
        if (M & 1u) y = (float)df_xbuf_get(xb, L) * f->b[L];
        for (uint16_t i = 0; i < L; i++)
            y += ((float)df_xbuf_get(xb, i) - (float)df_xbuf_get(xb, M-1u-i)) * f->b[i];
    } else {
        for (uint16_t i = 0; i < M; i++)
            y += (float)df_xbuf_get(xb, i) * f->b[i];
    }
    return y;
}

int16_t df_fir_float_tick16(df_fir_float_t *f, const df_xbuf_t *xb)
{
    return (int16_t)DF_ROUND_F(df_fir_float_tick(f, xb));
}

/* ===
 * FIR – integer (Q13, pre-scaled coefficients)
 *  */

void df_fir_int_init(df_fir_int_t *f, const df_fir_int_coeff_t *c)
{
    memset(f, 0, sizeof(*f));
    f->M = (uint16_t)DF_MIN(c->M, DF_MAX_FIR_TAPS);
    f->L = f->M >> 1;
    /* Copy pre-scaled Sb[] verbatim – no float conversion */
    for (uint16_t i = 0; i < f->M; i++) f->Sb[i] = c->Sb[i];

    f->sym_plus = f->sym_minus = true;
    for (uint16_t i = 0; i < f->L; i++) {
        if (f->Sb[i] != f->Sb[f->M-1-i])  f->sym_plus  = false;
        if (f->Sb[i] != -f->Sb[f->M-1-i]) f->sym_minus = false;
    }
}

int16_t df_fir_int_tick(df_fir_int_t *f, const df_xbuf_t *xb)
{
    int64_t y = 0;
    const uint16_t M = f->M, L = f->L;

    if (f->sym_plus) {
        if (M & 1u) y = (int32_t)df_xbuf_get(xb, L) * (int32_t)f->Sb[L];
        for (uint16_t i = 0; i < L; i++) {
            if (f->Sb[i])
                y = df_madd64(y,
                    (int32_t)df_xbuf_get(xb,i) + (int32_t)df_xbuf_get(xb,M-1u-i),
                    (int32_t)f->Sb[i]);
        }
    } else if (f->sym_minus) {
        if (M & 1u) y = (int32_t)df_xbuf_get(xb, L) * (int32_t)f->Sb[L];
        for (uint16_t i = 0; i < L; i++) {
            if (f->Sb[i])
                y = df_madd64(y,
                    (int32_t)df_xbuf_get(xb,i) - (int32_t)df_xbuf_get(xb,M-1u-i),
                    (int32_t)f->Sb[i]);
        }
    } else {
        for (uint16_t i = 0; i < M; i++) {
            if (f->Sb[i])
                y = df_madd64(y, (int32_t)df_xbuf_get(xb,i), (int32_t)f->Sb[i]);
        }
    }
    return (int16_t)(DF_ROUNDOFF(y, DF_R_FIR) >> DF_LDS_FIR);
}

/* ===
 * Biquad – floating-point
 *  */

void df_biquad_float_init(df_biquad_float_t *s,
                           float b0, float b1, float b2,
                           float a0, float a1, float a2)
{
    (void)a0;
    memset(s, 0, sizeof(*s));
    s->b[0]=b0; s->b[1]=b1; s->b[2]=b2;
    s->a[0]=1.0f; s->a[1]=a1; s->a[2]=a2;
}

float df_biquad_float_tick(df_biquad_float_t *s, float x)
{
    float y = s->b[0]*x + s->b[1]*s->x1 + s->b[2]*s->x2
                        - s->a[1]*s->y1  - s->a[2]*s->y2;
    s->x2=s->x1; s->x1=x;
    s->y2=s->y1; s->y1=y;
    return y;
}

/* ===
 * Biquad – integer (Q20, pre-scaled coefficients)
 *
 * Args are the raw int32_t values straight from the .utx sosi* rows.
 * The two-stage headroom trick (LDY_SOS extra bits for y*a products) is
 * preserved exactly from the reference design.
 *  */

void df_biquad_int_init(df_biquad_int_t *s,
                         int32_t Sb0, int32_t Sb1, int32_t Sb2,
                         int32_t Sa0, int32_t Sa1, int32_t Sa2)
{
    (void)Sa0; /* Sa0 = 1048576; kept for documentation symmetry, not used */
    memset(s, 0, sizeof(*s));
    s->Sb[0]=Sb0; s->Sb[1]=Sb1; s->Sb[2]=Sb2;
    s->Sa[0]=DF_S_SOS; s->Sa[1]=Sa1; s->Sa[2]=Sa2;
}

int16_t df_biquad_int_tick(df_biquad_int_t *s, int16_t x)
{
    int64_t y;
    y  = -((s->y1 >> (DF_LDS_SOS - DF_LDY_SOS)) * (int64_t)s->Sa[1]);
    y -= ((s->y2 >> (DF_LDS_SOS - DF_LDY_SOS)) * (int64_t)s->Sa[2]);
    y  = y >> DF_LDY_SOS;
    y += (int64_t)x       * s->Sb[0];
    y += (int64_t)s->x1   * s->Sb[1];
    y += (int64_t)s->x2   * s->Sb[2];

    s->y2=s->y1; s->y1=y;
    s->x2=s->x1; s->x1=x;
    return (int16_t)((y + DF_R_SOS) >> DF_LDS_SOS);
}

/* ===
 * SOS – floating-point
 *  */

void df_sos_float_init(df_sos_float_t *f, const df_sos_float_coeff_t *c)
{
    memset(f, 0, sizeof(*f));
    f->Nsos = DF_MIN(c->Nsos, DF_MAX_SOS_STAGES);
    for (uint8_t i = 0; i < f->Nsos; i++)
        df_biquad_float_init(&f->stage[i],
            c->sos[i][0], c->sos[i][1], c->sos[i][2],
            c->sos[i][3], c->sos[i][4], c->sos[i][5]);
}

float   df_sos_float_tick  (df_sos_float_t *f, float x)
{
    float y = x;
    for (uint8_t i = 0; i < f->Nsos; i++)
        y = df_biquad_float_tick(&f->stage[i], y);
    return y;
}

int16_t df_sos_float_tick16(df_sos_float_t *f, int16_t x)
{
    return (int16_t)DF_ROUND_F(df_sos_float_tick(f, (float)x));
}

/* ===
 * SOS – integer (Q20, pre-scaled coefficients from utx sosi* rows)
 *  */

void df_sos_int_init(df_sos_int_t *f, const df_sos_int_coeff_t *c)
{
    memset(f, 0, sizeof(*f));
    f->Nsos = DF_MIN(c->Nsos, DF_MAX_SOS_STAGES);
    for (uint8_t i = 0; i < f->Nsos; i++)
        df_biquad_int_init(&f->stage[i],
            c->sos[i][0], c->sos[i][1], c->sos[i][2],  /* Sb0 Sb1 Sb2 */
            c->sos[i][3], c->sos[i][4], c->sos[i][5]);  /* Sa0 Sa1 Sa2 */
}

int16_t df_sos_int_tick(df_sos_int_t *f, int16_t x)
{
    int16_t y = x;
    for (uint8_t i = 0; i < f->Nsos; i++)
        y = df_biquad_int_tick(&f->stage[i], y);
    return y;
}

/* ===
 * LTC – floating-point
 *  */

void df_ltc_float_init(df_ltc_float_t *f, const df_ltc_float_coeff_t *c)
{
    memset(f, 0, sizeof(*f));
    f->Nk  = DF_MIN(c->Nk,  DF_MAX_LTC_ORDER);
    f->Nnu = DF_MIN(c->Nnu, DF_MAX_LTC_ORDER + 1);
    for (uint8_t i = 0; i < f->Nk;  i++) f->k[i]  = c->k[i];
    for (uint8_t i = 0; i < f->Nnu; i++) f->nu[i]  = c->nu[i];
}

float df_ltc_float_tick(df_ltc_float_t *f, int16_t x)
{
    const int M = f->Nk;
    if (M <= 0) return 0.0f;
    f->ubuf[M] = (float)x;
    for (int i = M-1; i >= 0; i--)
        f->ubuf[i] = f->ubuf[i+1] - f->k[i] * f->vbuf[i];
    for (int i = 0; i < M; i++)
        f->vbuf[i+1] = f->wbuf[i] + f->k[i] * f->ubuf[i];
    f->vbuf[0] = f->ubuf[0];
    memcpy(f->wbuf, f->vbuf, (size_t)(M+1)*sizeof(float));

    float acc = f->nu[0] * f->ubuf[0];
    for (int i = 1; i <= M; i++)
        acc += f->vbuf[i] * f->nu[i];
    return acc;
}

int16_t df_ltc_float_tick16(df_ltc_float_t *f, int16_t x)
{
    return (int16_t)DF_ROUND_F(df_ltc_float_tick(f, x));
}

/* ===
 * LTC – integer (Q14, pre-scaled coefficients from utx ltci.* rows)
 *  */

void df_ltc_int_init(df_ltc_int_t *f, const df_ltc_int_coeff_t *c)
{
    memset(f, 0, sizeof(*f));
    f->Nk  = DF_MIN(c->Nk,  DF_MAX_LTC_ORDER);
    f->Nnu = DF_MIN(c->Nnu, DF_MAX_LTC_ORDER + 1);
    /* Copy pre-scaled Sk / Snu verbatim – no float conversion */
    for (uint8_t i = 0; i < f->Nk;  i++) f->Sk[i]  = c->Sk[i];
    for (uint8_t i = 0; i < f->Nnu; i++) f->Snu[i]  = c->Snu[i];
}

int16_t df_ltc_int_tick(df_ltc_int_t *f, int16_t x)
{
    const int M = f->Nk;
    if (M <= 0) return 0;

    f->ubuf[M] = (int32_t)x * DF_S_LTC;

    for (int i = M-1; i >= 0; i--) {
        f->ubuf[i] = f->ubuf[i+1]
                   - (int32_t)(((int64_t)f->Sk[i] * f->vbuf[i] + DF_R_LTC) >> DF_LDS_LTC);
    }
    for (int i = 0; i < M; i++) {
        f->vbuf[i+1] = f->wbuf[i]
                     + (int32_t)(((int64_t)f->Sk[i] * f->ubuf[i] + DF_R_LTC) >> DF_LDS_LTC);
    }
    f->vbuf[0] = f->ubuf[0];
    memcpy(f->wbuf, f->vbuf, (size_t)(M+1)*sizeof(int32_t));

    int64_t acc = ((int64_t)f->Snu[0] * f->ubuf[0] + DF_R_LTC) >> DF_LDS_LTC;
    for (int i = 1; i <= M; i++)
        acc += (((int64_t)f->vbuf[i] * f->Snu[i]) + DF_R_LTC) >> DF_LDS_LTC;

    return (int16_t)((acc + DF_R_LTC) >> DF_LDS_LTC);
}

/* ===
 * Generic handle
 *  */

void df_init(df_handle_t *h, filter_type_e type, const void *coeff,
             df_direct_form_e form, df_quant_mode_e qm)
{
    memset(h, 0, sizeof(*h));
    h->type = type;
    df_xbuf_init(&h->xbuf);
    switch (type) {
    case _IIR_FLOAT: df_iir_float_init(&h->state.iir_f, (const df_iir_float_coeff_t *)coeff); break;
    case _IIR_INT:   df_iir_int_init  (&h->state.iir_i, (const df_iir_int_coeff_t   *)coeff, form, qm); break;
    case _FIR_FLOAT: df_fir_float_init(&h->state.fir_f, (const df_fir_float_coeff_t *)coeff); break;
    case _FIR_INT:   df_fir_int_init  (&h->state.fir_i, (const df_fir_int_coeff_t   *)coeff); break;
    case _SOS_FLOAT: df_sos_float_init(&h->state.sos_f, (const df_sos_float_coeff_t *)coeff); break;
    case _SOS_INT:   df_sos_int_init  (&h->state.sos_i, (const df_sos_int_coeff_t   *)coeff); break;
    case _LTC_FLOAT: df_ltc_float_init(&h->state.ltc_f, (const df_ltc_float_coeff_t *)coeff); break;
    case _LTC_INT:   df_ltc_int_init  (&h->state.ltc_i, (const df_ltc_int_coeff_t   *)coeff); break;
    default: break;
    }
}

int16_t df_process(df_handle_t *h, int16_t x)
{
    switch (h->type) {
    case _IIR_FLOAT: df_xbuf_push(&h->xbuf,x); return df_iir_float_tick16(&h->state.iir_f, &h->xbuf);
    case _IIR_INT:   df_xbuf_push(&h->xbuf,x); return df_iir_int_tick    (&h->state.iir_i, &h->xbuf);
    case _FIR_FLOAT: df_xbuf_push(&h->xbuf,x); return df_fir_float_tick16(&h->state.fir_f, &h->xbuf);
    case _FIR_INT:   df_xbuf_push(&h->xbuf,x); return df_fir_int_tick    (&h->state.fir_i, &h->xbuf);
    case _SOS_FLOAT: return df_sos_float_tick16(&h->state.sos_f, x);
    case _SOS_INT:   return df_sos_int_tick    (&h->state.sos_i, x);
    case _LTC_FLOAT: return df_ltc_float_tick16(&h->state.ltc_f, x);
    case _LTC_INT:   return df_ltc_int_tick    (&h->state.ltc_i, x);
    default:         return 0;
    }
}

void df_reset(df_handle_t *h)
{
    df_xbuf_init(&h->xbuf);
    switch (h->type) {
    case _IIR_FLOAT:
        memset(h->state.iir_f.ybuf,0,sizeof(h->state.iir_f.ybuf));
        h->state.iir_f.nybuf=0; break;
    case _IIR_INT:
        memset(h->state.iir_i.ybuf,0,sizeof(h->state.iir_i.ybuf));
        memset(h->state.iir_i.wbuf,0,sizeof(h->state.iir_i.wbuf));
        h->state.iir_i.nybuf=h->state.iir_i.nwbuf=h->state.iir_i.n_sat=0; break;
    case _FIR_FLOAT: case _FIR_INT: break;  /* state is xbuf only */
    case _SOS_FLOAT:
        for(uint8_t i=0;i<h->state.sos_f.Nsos;i++){
            df_biquad_float_t *s=&h->state.sos_f.stage[i];
            s->x1=s->x2=s->y1=s->y2=0.0f;} break;
    case _SOS_INT:
        for(uint8_t i=0;i<h->state.sos_i.Nsos;i++){
            df_biquad_int_t *s=&h->state.sos_i.stage[i];
            s->x1=s->x2=0; s->y1=s->y2=0;} break;
    case _LTC_FLOAT:
        memset(h->state.ltc_f.ubuf,0,sizeof(h->state.ltc_f.ubuf));
        memset(h->state.ltc_f.vbuf,0,sizeof(h->state.ltc_f.vbuf));
        memset(h->state.ltc_f.wbuf,0,sizeof(h->state.ltc_f.wbuf)); break;
    case _LTC_INT:
        memset(h->state.ltc_i.ubuf,0,sizeof(h->state.ltc_i.ubuf));
        memset(h->state.ltc_i.vbuf,0,sizeof(h->state.ltc_i.vbuf));
        memset(h->state.ltc_i.wbuf,0,sizeof(h->state.ltc_i.wbuf)); break;
    default: break;
    }
}
