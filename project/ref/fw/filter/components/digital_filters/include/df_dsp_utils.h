#ifndef DF_DSP_UTILS_H
#define DF_DSP_UTILS_H

/**
 * @file    df_dsp_utils.h
 * @brief   DSP utility macros and inline helpers for the digital_filters component.
 *
 * Targets the ESP32-S3 (Xtensa LX7).  The ESP32-S3 does NOT have the ARM
 * SMLAL/SMULL instructions used by the Arduino Due (Cortex-M3), so the
 * generic 64-bit C path is used instead.  The compiler will emit efficient
 * Xtensa DSP sequences automatically at -O2/-O3.
 *
 * All symbols are prefixed with DF_ to avoid polluting the global namespace.
 */


#include <stdint.h>
#include <string.h>   /* memset */
#include <math.h>     /* roundf  */

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Generic arithmetic helpers
 * ---------------------------------------------------------------------- */

#ifndef DF_MIN
#  define DF_MIN(a, b)          (((a) < (b)) ? (a) : (b))
#endif
#ifndef DF_MAX
#  define DF_MAX(a, b)          (((a) > (b)) ? (a) : (b))
#endif
#ifndef DF_ABS
#  define DF_ABS(a)             (((a) < 0)   ? -(a) : (a))
#endif
#ifndef DF_LIMIT
#  define DF_LIMIT(x, lo, hi)   DF_MAX((lo), DF_MIN((x), (hi)))
#endif

/** Round a floating-point value to the nearest integer (away from zero at .5). */
#ifndef DF_ROUND_F
#  define DF_ROUND_F(a)         ((int32_t)(((a) < 0.0f) ? ((a) - 0.5f) : ((a) + 0.5f)))
#endif

/** Add a rounding offset R before a right-shift (R = S/2 = 1 << (lds-1)). */
#ifndef DF_ROUNDOFF
#  define DF_ROUNDOFF(a, r)     (((a) < 0) ? ((a) - (r)) : ((a) + (r)))
#endif

/* -------------------------------------------------------------------------
 * 64-bit multiply-accumulate
 *   On Xtensa LX7 the compiler will lower this to a pair of MUL32 + ADD
 *   instructions; no inline asm needed.
 * ---------------------------------------------------------------------- */

/** Multiply two 32-bit integers and accumulate into a 64-bit result. */
static inline int64_t df_madd64(int64_t sum, int32_t x, int32_t y)
{
    return sum + (int64_t)x * (int64_t)y;
}

/* -------------------------------------------------------------------------
 * Fixed-point scaling constants
 *
 *  IIR / LTC integer path:  scaling = 2^20  (Q20)
 *  FIR integer path:        scaling = 2^13  (Q13)
 *  SOS integer path:        scaling = 2^20  (Q20, configurable per stage)
 * ---------------------------------------------------------------------- */

/* IIR integer */
#define DF_LDS_IIR      20
#define DF_S_IIR        (1 << DF_LDS_IIR)
#define DF_R_IIR        (1 << (DF_LDS_IIR - 1))
#define DF_LDY_IIR      8   /**< extra headroom shift for DF-1 y*a products */

/* FIR integer */
#define DF_LDS_FIR      13
#define DF_S_FIR        (1 << DF_LDS_FIR)
#define DF_R_FIR        (1 << (DF_LDS_FIR - 1))

/* SOS integer */
#define DF_LDS_SOS      20
#define DF_S_SOS        (1 << DF_LDS_SOS)
#define DF_R_SOS        (1 << (DF_LDS_SOS - 1))
#define DF_LDY_SOS      15  /**< headroom shift for y*a products in biquad */

/* LTC integer */
#define DF_LDS_LTC      14
#define DF_S_LTC        (1 << DF_LDS_LTC)
#define DF_R_LTC        (1 << (DF_LDS_LTC - 1))

/* -------------------------------------------------------------------------
 * Buffer / order limits
 * ---------------------------------------------------------------------- */

#define DF_MAX_IIR_TAPS     33   /**< Max IIR numerator/denominator length    */
#define DF_MAX_FIR_TAPS    256   /**< Max FIR taps                            */
#define DF_MAX_SOS_STAGES   16   /**< Max second-order sections               */
#define DF_MAX_LTC_ORDER    33   /**< Max lattice order                       */
#define DF_XBUF_SIZE       256   /**< Input sample circular buffer size       */

#ifdef __cplusplus
}
#endif


#endif // DF_DSP_UTILS_H