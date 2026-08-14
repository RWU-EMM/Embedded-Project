#ifndef DF_XBUF_H
#define DF_XBUF_H


/**
 * @file    df_xbuf.h
 * @brief   Circular input-sample buffer used by IIR and FIR filters.
 *
 * Wraps a power-of-two ring buffer so that past samples x[n-k] can be
 * retrieved with a single index calculation and no modulo division.
 */


#include <stdint.h>
#include <string.h>
#include "df_dsp_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Data structure
 * ---------------------------------------------------------------------- */

/**
 * @brief Circular sample buffer holding the last DF_XBUF_SIZE input samples.
 *
 * Write new samples with df_xbuf_push(), read past samples with
 * df_xbuf_get().
 */
typedef struct {
    int16_t  buf[DF_XBUF_SIZE]; /**< sample storage                         */
    uint16_t idx;               /**< index of the most-recently written cell */
} df_xbuf_t;

/* -------------------------------------------------------------------------
 * Inline API
 * ---------------------------------------------------------------------- */

/** Initialise (zero) the buffer. */
static inline void df_xbuf_init(df_xbuf_t *xb)
{
    memset(xb->buf, 0, sizeof(xb->buf));
    xb->idx = DF_XBUF_SIZE - 1u;
}

/**
 * @brief Push a new input sample into the buffer.
 * @param xb  Pointer to buffer.
 * @param x   New sample x[n].
 */
static inline void df_xbuf_push(df_xbuf_t *xb, int16_t x)
{
    xb->idx++;
    if (xb->idx >= DF_XBUF_SIZE) xb->idx = 0u;
    xb->buf[xb->idx] = x;
}

/**
 * @brief Retrieve a past sample x[n - delay].
 * @param xb     Pointer to buffer.
 * @param delay  Number of samples back (0 = x[n], 1 = x[n-1], …).
 * @return       The sample value.
 */
static inline int16_t df_xbuf_get(const df_xbuf_t *xb, uint16_t delay)
{
    int32_t i = (int32_t)xb->idx - (int32_t)delay;
    if (i < 0) i += DF_XBUF_SIZE;
    return xb->buf[(uint16_t)i];
}

#ifdef __cplusplus
}
#endif


#endif // DF_XBUF_H