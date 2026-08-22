#include "config.h"
#include "gpio.h"
#include "uart.h"
#include "adc.h"
#include "dac.h"
#include "timer.h"
#include "digital_filters.h"

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/* --------------------------------------------------------------------------
 * Filter configuration
 * -------------------------------------------------------------------------- */

#define FILTER_DEFAULT_TYPE _IIR_FLOAT
#define FILTER_DIRECT_FORM DF_DIRECT_FORM_1
#define FILTER_QUANT_MODE DF_QUANT_ROUND

#define UART_LINE_BUFFER_SIZE (512U)
#define UART_IDLE_RELEASE_MS (5UL)
#define ADC_CENTER (2048)
#define DAC_MAX_VALUE ((1U << DAC_RESOLUTION_BITS) - 1U)

/* The ESP32 implementation receives signed, centred ADC samples:
 *     raw 0..4095 -> sample -2048..2047
 * Keep exactly that signal convention on Nano 33 IoT.
 */

static df_handle_t s_filter;
static filter_type_e s_active_filter = FILTER_DEFAULT_TYPE;
static bool s_filter_initialized = false;

/* Runtime coefficient upload state. The format is the same line-oriented
 * "w <key> <values...>" protocol used by the ESP32 implementation. */
typedef enum {
  COEFF_IDLE = 0,
  COEFF_WAITING,
  COEFF_DONE_OK,
  COEFF_DONE_FAIL
} coeff_state_t;

static coeff_state_t s_coeff_state = COEFF_IDLE;
static filter_type_e s_coeff_target;
static const void *s_coeff_override = NULL;
static bool s_have_primary = false;
static bool s_have_primary_n = false;
static bool s_have_secondary = false;
static bool s_have_secondary_n = false;
static uint8_t s_sosf_rows_seen = 0U;
static uint8_t s_sosi_rows_seen = 0U;

static df_iir_float_coeff_t s_iirf_upload;
static df_iir_int_coeff_t s_iiri_upload;
static df_fir_float_coeff_t s_firf_upload;
static df_fir_int_coeff_t s_firi_upload;
static df_sos_float_coeff_t s_sosf_upload;
static df_sos_int_coeff_t s_sosi_upload;
static df_ltc_float_coeff_t s_ltcf_upload;
static df_ltc_int_coeff_t s_ltci_upload;

/* --------------------------------------------------------------------------
 * Coefficient bank copied from the ESP32 filter configuration.
 * These are the same besselLP9 / 555 Hz / Fs=5 kHz coefficients supplied in
 * filter_config.h. Integer coefficients are already quantized/scaled.
 * -------------------------------------------------------------------------- */
//@warning: GCC 7.2 (the compiler used by the SAMD core) running in C++ mode, does not support 2D array designated
//          initializers remove field labels inside standard array definitions when compiling under C++11/C++14.
static const df_iir_float_coeff_t s_coeff_iir_float = {
  /*.b =*/{ 0.000852933555217f, 0.00767640199695f, 0.0307056079878f,
            0.0716464186382f, 0.107469627957f, 0.107469627957f,
            0.0716464186382f, 0.0307056079878f, 0.00767640199695f,
            0.000852933555217f },
  /*.a =*/{ 1.0f, -1.51812930171f, 1.75535760798f, -1.30615492445f, 0.729096023248f, -0.294232362116f, 0.085905807656f, -0.0171237790051f, 0.00210303765231f, -0.000120128979675f },
  /*.Nb =*/10,
  /*.Na = */ 10
};

static const df_iir_int_coeff_t s_coeff_iir_int = {
  /*.Sb =*/{ 894, 8049, 32197, 75127, 112690, 112690, 75127, 32197, 8049, 894 },
  /*.Sa =*/{ 1048576, -1591874, 1840626, -1369603, 764513, -308525, 90079, -17956, 2205, -126 },
  /*.Nb =*/10,
  /*.Na =*/10
};

static const df_fir_float_coeff_t s_coeff_fir_float = {
  /*.b =*/{ 0.000852933555217f, 0.00897126541954f, 0.0428279454894f,
            0.122031061974f, 0.228646190953f, 0.290025935741f,
            0.241319611581f, 0.109481024075f, -0.00522754130118f,
            -0.0380109122215f, -0.0137816433297f, 0.00890319219537f,
            0.00785340094475f, -0.00138649677679f, -0.00354621798539f,
            -0.000263759330188f, 0.00146438253225f, 0.000454298559491f,
            -0.000545539755738f, -0.000330843376568f, 0.000168908814841f },
  /*.M =*/21
};

static const df_fir_int_coeff_t s_coeff_fir_int = {
  /*.Sb =*/{ 7, 73, 351, 1000, 1873, 2376, 1977, 897, -43, -311,
             -113, 73, 64, -11, -29, -2, 12, 4, -4, -3, 1 },
  /*.M =*/21
};

static const df_sos_float_coeff_t s_coeff_sos_float = {
  /* .sos = */ {
    { 0.243322931475f, 0.466162375192f, 0.22331474186f, 1.0f, -0.24245244434f, 0.456702095896f },
    { 0.243322931475f, 0.473694118923f, 0.230901384066f, 1.0f, -0.33203214577f, 0.22492322465f },
    { 0.243322931475f, 0.488897241239f, 0.246170221857f, 1.0f, -0.367561721959f, 0.110788880202f },
    { 0.243322931475f, 0.505075906794f, 0.262401080697f, 1.0f, -0.382638061655f, 0.0545666085897f },
    { 0.243322931475f, 0.256076741132f, 0.0f, 1.0f, -0.193444927988f, 0.0f } },
  /* .Nsos = */ 5
};

static const df_sos_int_coeff_t s_coeff_sos_int = {
  /* .sos = */ {
    { 255143, 488807, 234162, 1048576, -254230, 478887 },
    { 255143, 496704, 242118, 1048576, -348161, 235849 },
    { 255143, 512646, 258128, 1048576, -385416, 116171 },
    { 255143, 529610, 275147, 1048576, -401225, 57217 },
    { 255143, 268516, 0, 1048576, -202842, 0 } },
  /* .Nsos = */ 5
};

static const df_ltc_float_coeff_t s_coeff_ltc_float = {
  /* nu */
  { -0.01366277258327422f, -0.01651811304526455f, 0.07474913864231153f,
    0.2250065517632938f, 0.2876764697249627f, 0.2285434841039772f,
    0.12202991579742f, 0.04282794341947878f, 0.008971265419540488f,
    0.0008529335552171375f },
  /* k */
  { -0.5512823103959078f, 0.6787790885725167f, -0.5040706107711028f,
    0.3518170320817122f, -0.1750105815454953f, 0.06114063990974815f,
    -0.01399714213495528f, 0.00192066635599589f, -0.0001201289796754797f },
  /* Nnu */ 10,
  /* Nk  */ 9
};

static const df_ltc_int_coeff_t s_coeff_ltc_int = {
  /* Snu */ { -224, -271, 1225, 3687, 4713, 3744, 1999, 702, 147, 14 },
  /* Sk  */ { -9032, 11121, -8259, 5764, -2867, 1002, -229, 31, -2 },
  /* Nnu */ 10,
  /* Nk  */ 9
};

static const void *filter_get_coeff(filter_type_e type) {
  if (s_coeff_override != NULL) {
    return s_coeff_override;
  }

  switch (type) {
    case _IIR_FLOAT: return &s_coeff_iir_float;
    case _IIR_INT: return &s_coeff_iir_int;
    case _FIR_FLOAT: return &s_coeff_fir_float;
    case _FIR_INT: return &s_coeff_fir_int;
    case _SOS_FLOAT: return &s_coeff_sos_float;
    case _SOS_INT: return &s_coeff_sos_int;
    case _LTC_FLOAT: return &s_coeff_ltc_float;
    case _LTC_INT: return &s_coeff_ltc_int;
    default: return &s_coeff_iir_float;
  }
}

/* --------------------------------------------------------------------------
 * Runtime coefficient loader
 * -------------------------------------------------------------------------- */

static int parse_floats(const char *text, float *out, int max_count) {
  int count = 0;
  char *token;
  char *save_inner = NULL;
  char *copy = strdup(text);

  if (copy == NULL) {
    return 0;
  }

  token = strtok_r(copy, " \t", &save_inner);
  while (token != NULL && count < max_count) {
    out[count++] = strtof(token, NULL);
    token = strtok_r(NULL, " \t", &save_inner);
  }

  free(copy);
  return count;
}

static int parse_ints(const char *text, int32_t *out, int max_count) {
  int count = 0;
  char *token;
  char *save_inner = NULL;
  char *copy = strdup(text);

  if (copy == NULL) {
    return 0;
  }

  token = strtok_r(copy, " \t", &save_inner);
  while (token != NULL && count < max_count) {
    out[count++] = (int32_t)strtol(token, NULL, 10);
    token = strtok_r(NULL, " \t", &save_inner);
  }

  free(copy);
  return count;
}

static void coeff_loader_begin(filter_type_e target) {
  s_coeff_target = target;
  s_coeff_state = COEFF_WAITING;
  s_have_primary = false;
  s_have_primary_n = false;
  s_have_secondary = false;
  s_have_secondary_n = false;
  s_sosf_rows_seen = 0U;
  s_sosi_rows_seen = 0U;

  memset(&s_iirf_upload, 0, sizeof(s_iirf_upload));
  memset(&s_iiri_upload, 0, sizeof(s_iiri_upload));
  memset(&s_firf_upload, 0, sizeof(s_firf_upload));
  memset(&s_firi_upload, 0, sizeof(s_firi_upload));
  memset(&s_sosf_upload, 0, sizeof(s_sosf_upload));
  memset(&s_sosi_upload, 0, sizeof(s_sosi_upload));
  memset(&s_ltcf_upload, 0, sizeof(s_ltcf_upload));
  memset(&s_ltci_upload, 0, sizeof(s_ltci_upload));
}

static void __attribute__((unused)) coeff_loader_abort(void) {
  s_coeff_state = COEFF_IDLE;
}

static void coeff_loader_feed_line(const char *line) {
  char key[32];
  const char *values;
  int key_len = 0;
  bool done = false;

  if (s_coeff_state != COEFF_WAITING) {
    return;
  }

  if (line[0] == '#' || line[0] == '\0') {
    return;
  }

  if (line[0] != 'w' || line[1] != ' ') {
    return;
  }

  values = line + 2;
  while (*values != '\0' && *values != ' ' && key_len < (int)sizeof(key) - 1) {
    key[key_len++] = *values++;
  }
  key[key_len] = '\0';

  while (*values == ' ') {
    ++values;
  }

  switch (s_coeff_target) {
    case _IIR_FLOAT:
      if (!strcmp(key, "iirf.b")) {
        s_iirf_upload.Nb = (uint8_t)parse_floats(values, s_iirf_upload.b, DF_MAX_IIR_TAPS);
        s_have_primary = true;
      } else if (!strcmp(key, "iirf.Nb")) {
        s_iirf_upload.Nb = (uint8_t)atoi(values);
        s_have_primary_n = true;
      } else if (!strcmp(key, "iirf.a")) {
        s_iirf_upload.Na = (uint8_t)parse_floats(values, s_iirf_upload.a, DF_MAX_IIR_TAPS);
        s_have_secondary = true;
      } else if (!strcmp(key, "iirf.Na")) {
        s_iirf_upload.Na = (uint8_t)atoi(values);
        s_have_secondary_n = true;
      }
      break;

    case _IIR_INT:
      if (!strcmp(key, "iiri.Sb")) {
        s_iiri_upload.Nb = (uint8_t)parse_ints(values, s_iiri_upload.Sb, DF_MAX_IIR_TAPS);
        s_have_primary = true;
      } else if (!strcmp(key, "iiri.Nb")) {
        s_iiri_upload.Nb = (uint8_t)atoi(values);
        s_have_primary_n = true;
      } else if (!strcmp(key, "iiri.Sa")) {
        s_iiri_upload.Na = (uint8_t)parse_ints(values, s_iiri_upload.Sa, DF_MAX_IIR_TAPS);
        s_have_secondary = true;
      } else if (!strcmp(key, "iiri.Na")) {
        s_iiri_upload.Na = (uint8_t)atoi(values);
        s_have_secondary_n = true;
      }
      break;

    case _FIR_FLOAT:
      if (strncmp(key, "firf.b", 6) == 0) {
        int offset = 0;
        if (key[6] == '+') {
          offset = atoi(key + 7);
        }
        if (offset >= 0 && offset < DF_MAX_FIR_TAPS) {
          int count = parse_floats(values,
                                   &s_firf_upload.b[offset],
                                   DF_MAX_FIR_TAPS - offset);
          if (offset + count > s_firf_upload.M) {
            s_firf_upload.M = (uint16_t)(offset + count);
          }
        }
        s_have_primary = true;
      } else if (!strcmp(key, "firf.N") || !strcmp(key, "firf.M")) {
        s_firf_upload.M = (uint16_t)atoi(values);
        s_have_primary = true;
        s_have_primary_n = true;
        s_have_secondary = true;
        s_have_secondary_n = true;
      }
      break;

    case _FIR_INT:
      {
        int32_t temp[DF_MAX_FIR_TAPS];
        if (strncmp(key, "firi.Sb", 7) == 0) {
          int offset = 0;
          int count;
          if (key[7] == '+') {
            offset = atoi(key + 8);
          }
          if (offset >= 0 && offset < DF_MAX_FIR_TAPS) {
            count = parse_ints(values, temp, DF_MAX_FIR_TAPS);
            if (offset + count > DF_MAX_FIR_TAPS) {
              count = DF_MAX_FIR_TAPS - offset;
            }
            for (int i = 0; i < count; ++i) {
              s_firi_upload.Sb[offset + i] = (int16_t)temp[i];
            }
            if (offset + count > s_firi_upload.M) {
              s_firi_upload.M = (uint16_t)(offset + count);
            }
          }
          s_have_primary = true;
        } else if (!strcmp(key, "firi.N") || !strcmp(key, "firi.M")) {
          s_firi_upload.M = (uint16_t)atoi(values);
          s_have_primary = true;
          s_have_primary_n = true;
          s_have_secondary = true;
          s_have_secondary_n = true;
        }
        break;
      }

    case _SOS_FLOAT:
      if (key[0] == 's' && key[1] == 'o' && key[2] == 's' && key[3] == 'f' && key[4] >= '0' && key[4] <= '9') {
        int index = atoi(key + 4);
        float values6[6];
        int count = parse_floats(values, values6, 6);
        if (count == 6 && index >= 0 && index < DF_MAX_SOS_STAGES) {
          memcpy(s_sosf_upload.sos[index], values6, sizeof(values6));
          ++s_sosf_rows_seen;
          s_have_primary = true;
        }
      } else if (!strcmp(key, "sosf.N")) {
        s_sosf_upload.Nsos = (uint8_t)atoi(values);
        s_have_primary_n = true;
        s_have_secondary = true;
        s_have_secondary_n = true;
      }
      break;

    case _SOS_INT:
      if (key[0] == 's' && key[1] == 'o' && key[2] == 's' && key[3] == 'i' && key[4] >= '0' && key[4] <= '9') {
        int index = atoi(key + 4);
        int32_t values6[6];
        int count = parse_ints(values, values6, 6);
        if (count == 6 && index >= 0 && index < DF_MAX_SOS_STAGES) {
          memcpy(s_sosi_upload.sos[index], values6, sizeof(values6));
          ++s_sosi_rows_seen;
          s_have_primary = true;
        }
      } else if (!strcmp(key, "sosi.N")) {
        s_sosi_upload.Nsos = (uint8_t)atoi(values);
        s_have_primary_n = true;
        s_have_secondary = true;
        s_have_secondary_n = true;
      }
      break;

    case _LTC_FLOAT:
      if (!strcmp(key, "ltcf.nu")) {
        s_ltcf_upload.Nnu = (uint8_t)parse_floats(values, s_ltcf_upload.nu, DF_MAX_LTC_ORDER + 1);
        s_have_primary = true;
      } else if (!strcmp(key, "ltcf.Nnu")) {
        s_ltcf_upload.Nnu = (uint8_t)atoi(values);
        s_have_primary_n = true;
      } else if (!strcmp(key, "ltcf.k")) {
        s_ltcf_upload.Nk = (uint8_t)parse_floats(values, s_ltcf_upload.k, DF_MAX_LTC_ORDER + 1);
        s_have_secondary = true;
      } else if (!strcmp(key, "ltcf.Nk")) {
        s_ltcf_upload.Nk = (uint8_t)atoi(values);
        s_have_secondary_n = true;
      }
      break;

    case _LTC_INT:
      if (!strcmp(key, "ltci.Snu")) {
        s_ltci_upload.Nnu = (uint8_t)parse_ints(values, s_ltci_upload.Snu, DF_MAX_LTC_ORDER + 1);
        s_have_primary = true;
      } else if (!strcmp(key, "ltci.Nnu")) {
        s_ltci_upload.Nnu = (uint8_t)atoi(values);
        s_have_primary_n = true;
      } else if (!strcmp(key, "ltci.Sk")) {
        s_ltci_upload.Nk = (uint8_t)parse_ints(values, s_ltci_upload.Sk, DF_MAX_LTC_ORDER + 1);
        s_have_secondary = true;
      } else if (!strcmp(key, "ltci.Nk")) {
        s_ltci_upload.Nk = (uint8_t)atoi(values);
        s_have_secondary_n = true;
      }
      break;

    default:
      break;
  }

  switch (s_coeff_target) {
    case _SOS_FLOAT:
      done = s_have_primary_n && s_sosf_rows_seen >= s_sosf_upload.Nsos;
      break;
    case _SOS_INT:
      done = s_have_primary_n && s_sosi_rows_seen >= s_sosi_upload.Nsos;
      break;
    default:
      done = s_have_primary && s_have_primary_n && s_have_secondary && s_have_secondary_n;
      break;
  }

  if (done) {
    s_coeff_state = COEFF_DONE_OK;
  }
}

static bool coeff_loader_commit(void) {
  if (s_coeff_state != COEFF_DONE_OK) {
    return false;
  }

  switch (s_coeff_target) {
    case _IIR_FLOAT:
      if (s_iirf_upload.Nb == 0U || s_iirf_upload.Na == 0U || s_iirf_upload.a[0] == 0.0f) return false;
      s_coeff_override = &s_iirf_upload;
      break;
    case _IIR_INT:
      if (s_iiri_upload.Nb == 0U || s_iiri_upload.Na == 0U || s_iiri_upload.Sa[0] == 0) return false;
      s_coeff_override = &s_iiri_upload;
      break;
    case _FIR_FLOAT:
      if (s_firf_upload.M == 0U || s_firf_upload.M > DF_MAX_FIR_TAPS) return false;
      s_coeff_override = &s_firf_upload;
      break;
    case _FIR_INT:
      if (s_firi_upload.M == 0U || s_firi_upload.M > DF_MAX_FIR_TAPS) return false;
      s_coeff_override = &s_firi_upload;
      break;
    case _SOS_FLOAT:
      if (s_sosf_upload.Nsos == 0U || s_sosf_upload.Nsos > DF_MAX_SOS_STAGES) return false;
      s_coeff_override = &s_sosf_upload;
      break;
    case _SOS_INT:
      if (s_sosi_upload.Nsos == 0U || s_sosi_upload.Nsos > DF_MAX_SOS_STAGES) return false;
      s_coeff_override = &s_sosi_upload;
      break;
    case _LTC_FLOAT:
      if (s_ltcf_upload.Nnu == 0U || s_ltcf_upload.Nk == 0U || s_ltcf_upload.Nk > DF_MAX_LTC_ORDER || s_ltcf_upload.Nnu > DF_MAX_LTC_ORDER + 1U) return false;
      s_coeff_override = &s_ltcf_upload;
      break;
    case _LTC_INT:
      if (s_ltci_upload.Nnu == 0U || s_ltci_upload.Nk == 0U || s_ltci_upload.Nk > DF_MAX_LTC_ORDER || s_ltci_upload.Nnu > DF_MAX_LTC_ORDER + 1U) return false;
      s_coeff_override = &s_ltci_upload;
      break;
    default:
      return false;
  }

  s_coeff_state = COEFF_IDLE;
  return filter_apply_type(s_coeff_target);
}

/* --------------------------------------------------------------------------
 * UART command parsing
 * -------------------------------------------------------------------------- */

static char s_uart_line[UART_LINE_BUFFER_SIZE];
static uint16_t s_uart_line_used = 0U;
static bool s_uart_received_line = false;
static uint32_t s_uart_last_rx_ms = 0U;

static int fmode_str_to_enum(const char *name) {
  static const char *names[] = {
    "iiri", "iirf", "firi", "firf",
    "sosi", "sosf", "ltci", "ltcf"
  };

  static const filter_type_e values[] = {
    _IIR_INT, _IIR_FLOAT, _FIR_INT, _FIR_FLOAT,
    _SOS_INT, _SOS_FLOAT, _LTC_INT, _LTC_FLOAT
  };

  for (uint8_t i = 0U; i < 8U; ++i) {
    if (strcmp(name, names[i]) == 0) {
      return (int)values[i];
    }
  }

  return -1;
}

static bool filter_apply_type(filter_type_e type) {
  const void *coeff = filter_get_coeff(type);

  df_init(&s_filter,
          type,
          coeff,
          FILTER_DIRECT_FORM,
          FILTER_QUANT_MODE);

  s_active_filter = type;
  s_filter_initialized = true;
  return true;
}

/* Parse one complete command using the same command names as the ESP32 code.
 * Commands relevant to the Nano implementation are fmode:<name> and stop/start.
 * SIM/USB/UDP commands are intentionally ignored.
 */
static void uart_process_line(char *line) {
  size_t length = strlen(line);

  while (length > 0U && (line[length - 1U] == '\r' || line[length - 1U] == '\n')) {
    line[length - 1U] = '\0';
    --length;
  }

  if (length == 0U) {
    return;
  }

  /* During coefficient upload every subsequent line is a coefficient line. */
  if (s_coeff_state == COEFF_WAITING) {
    coeff_loader_feed_line(line);
    if (s_coeff_state == COEFF_DONE_OK) {
      (void)coeff_loader_commit();
    }
    return;
  }

  if (strncmp(line, "fmode:", 6U) == 0) {
    int type = fmode_str_to_enum(line + 6U);
    if (type >= 0) {
      /* A normal fmode command selects the built-in coefficient bank. */
      s_coeff_override = NULL;
      filter_apply_type((filter_type_e)type);
    }
    return;
  }

  if (strncmp(line, "scoeff:", 7U) == 0) {
    int type = fmode_str_to_enum(line + 7U);
    if (type >= 0) {
      timer_disable();
      coeff_loader_begin((filter_type_e)type);
    }
    return;
  }

  if (strcmp(line, "reset") == 0) {
    s_coeff_override = NULL;
    filter_apply_type(FILTER_DEFAULT_TYPE);
    return;
  }

  if (strcmp(line, "stop") == 0) {
    timer_disable();
    return;
  }

  if (strcmp(line, "start") == 0) {
    if (gpio_get_cmd()) {
      timer_enable();
    }
    return;
  }

  /* imode:, freq:, amp:, wave:, gcoeff and USB/UDP commands intentionally
     * have no function in this Nano-only implementation. */
}

static void uart_collect(void) {
  while (uart_available()) {
    int value = uart_read_byte();
    if (value < 0) {
      break;
    }

    s_uart_last_rx_ms = millis();

    if (value == '\n') {
      s_uart_line[s_uart_line_used] = '\0';
      uart_process_line(s_uart_line);
      s_uart_line_used = 0U;
      s_uart_received_line = true;
      continue;
    }

    if (value == '\r') {
      continue;
    }

    if (s_uart_line_used < (UART_LINE_BUFFER_SIZE - 1U)) {
      s_uart_line[s_uart_line_used++] = (char)value;
    } else {
      /* Discard the current overlong command and wait for its newline. */
      s_uart_line_used = 0U;
    }
  }
}

/* --------------------------------------------------------------------------
 * GPIO-controlled execution state
 * -------------------------------------------------------------------------- */

static bool s_uart_mode = false;

static void enter_uart_mode(void) {
  if (s_uart_mode) {
    return;
  }

  timer_disable();
  gpio_set_rdy(false);
  s_uart_mode = true;
  s_uart_received_line = false;
  s_uart_last_rx_ms = millis();
}

static void service_uart_mode(void) {
  uart_collect();

  /* A command is considered complete after its newline has been received
     * and the UART has remained idle for the configured short interval. */
  if (s_uart_received_line && s_coeff_state != COEFF_WAITING && !uart_available() && (uint32_t)(millis() - s_uart_last_rx_ms) >= UART_IDLE_RELEASE_MS) {
    gpio_set_rdy(true);
  }

  /* The timer is restarted only after CMD returns high. */
  if (gpio_get_cmd()) {
    s_uart_mode = false;
    s_uart_received_line = false;
    timer_enable();
    gpio_set_rdy(true);
  }
}

/* --------------------------------------------------------------------------
 * Timer-driven acquisition -> filter -> DAC
 * -------------------------------------------------------------------------- */

static void process_one_sample(void) {
  uint16_t adc_value;
  int16_t adc_sample;
  int16_t filtered_sample;
  uint16_t dac_value;

  adc_value = adc_read();

  /* Match ESP32 signal representation: signed, centred 12-bit sample. */
  adc_sample = (int16_t)((int32_t)adc_value - ADC_CENTER);

  filtered_sample = df_process(&s_filter, adc_sample);

  /* Filter output is a signed 12-bit-domain value. Convert it to the
     * Nano's 10-bit DAC domain without changing the signal midpoint. */
  int32_t dac_signed = ((int32_t)filtered_sample * (int32_t)DAC_MAX_VALUE) / 4095L;
  int32_t dac_output = dac_signed + ((int32_t)DAC_MAX_VALUE / 2L);

  if (dac_output < 0L) {
    dac_output = 0L;
  } else if (dac_output > (int32_t)DAC_MAX_VALUE) {
    dac_output = (int32_t)DAC_MAX_VALUE;
  }

  dac_value = (uint16_t)dac_output;
  dac_write(dac_value);
}

/* --------------------------------------------------------------------------
 * Arduino/Serial1 bridge
 * -------------------------------------------------------------------------- */

extern "C" void uart_port_init(uint32_t baud_rate,
                               uint8_t data_bits,
                               uint8_t stop_bits,
                               uint8_t parity) {
  uint32_t serial_config = SERIAL_8N1;

  if (data_bits == 8U && stop_bits == 1U) {
    if (parity == 1U) {
      serial_config = SERIAL_8E1;
    } else if (parity == 2U) {
      serial_config = SERIAL_8O1;
    }
  }

  Serial1.begin(baud_rate, serial_config);
}

extern "C" int uart_port_available(void) {
  return Serial1.available();
}

extern "C" int uart_port_read_byte(void) {
  return Serial1.read();
}

extern "C" int uart_port_write_byte(uint8_t data) {
  return (Serial1.write(data) == 1U) ? 1 : 0;
}

/* --------------------------------------------------------------------------
 * Application entry points
 * -------------------------------------------------------------------------- */

void setup(void) {
  gpio_init();
  uart_init();
  adc_init();
  dac_init();
  timer_init();

  filter_apply_type(FILTER_DEFAULT_TYPE);

  /* System starts in sampling mode. */
  gpio_set_rdy(true);
  timer_enable();
}

void loop(void) {
  /* CMD is active-low. Enter UART/service mode immediately when it falls. */
  if (!gpio_get_cmd()) {
    enter_uart_mode();
  }

  if (s_uart_mode) {
    service_uart_mode();
    return;
  }

  /* The actual 5 kHz work is released by the TC3 interrupt. */
  while (timer_take_event()) {
    process_one_sample();
  }
}
