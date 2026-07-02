#include "coeff_loader.h"
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_err.h"

static coeff_load_state_e s_state = CL_IDLE;
static filter_type_e s_target;

/* one scratch struct per type, reused */
static df_iir_float_coeff_t s_iirf;
static df_iir_int_coeff_t s_iiri;
static df_fir_float_coeff_t s_firf;
static df_fir_int_coeff_t s_firi;
static df_sos_float_coeff_t s_sosf;
static df_sos_int_coeff_t s_sosi;
static df_ltc_float_coeff_t s_ltcf;
static df_ltc_int_coeff_t s_ltci;

static uint8_t s_sosf_rows_seen, s_sosi_rows_seen;

/* completion flags - set true once both halves (b/a or nu/k) seen with matching N */
static bool s_have_primary, s_have_primary_n, s_have_secondary, s_have_secondary_n;

// static int parse_floats(const char *s, float *out, int max)
// {
//     int n = 0;
//     char *tok, *str = strdup(s);
//     char *save = str;
//     tok = strtok(str, " \t");
//     while (tok && n < max)
//     {
//         out[n++] = strtof(tok, NULL);
//         tok = strtok(NULL, " \t");
//     }
//     free(save);
//     return n;
// }

// static int parse_ints(const char *s, int32_t *out, int max)
// {
//     int n = 0;
//     char *tok, *str = strdup(s);
//     char *save = str;
//     tok = strtok(str, " \t");
//     while (tok && n < max)
//     {
//         out[n++] = (int32_t)strtol(tok, NULL, 10);
//         tok = strtok(NULL, " \t");
//     }
//     free(save);
//     return n;
// }

static int parse_floats(const char *s, float *out, int max)
{
    int n = 0;
    char *tok, *save_inner;
    char *str = strdup(s);
    char *save = str;
    tok = strtok_r(str, " \t", &save_inner);
    while (tok && n < max)
    {
        out[n++] = strtof(tok, NULL);
        tok = strtok_r(NULL, " \t", &save_inner);
    }
    free(save);
    return n;
}

static int parse_ints(const char *s, int32_t *out, int max)
{
    int n = 0;
    char *tok, *save_inner;
    char *str = strdup(s);
    char *save = str;
    tok = strtok_r(str, " \t", &save_inner);
    while (tok && n < max)
    {
        out[n++] = (int32_t)strtol(tok, NULL, 10);
        tok = strtok_r(NULL, " \t", &save_inner);
    }
    free(save);
    return n;
}

void coeff_loader_begin(filter_type_e target)
{
    s_target = target;
    s_state = CL_WAITING;
    s_have_primary = s_have_primary_n = s_have_secondary = s_have_secondary_n = false;
    memset(&s_iirf, 0, sizeof s_iirf);
    memset(&s_iiri, 0, sizeof s_iiri);
    memset(&s_firf, 0, sizeof s_firf);
    memset(&s_firi, 0, sizeof s_firi);
    memset(&s_sosf, 0, sizeof s_sosf);
    memset(&s_sosi, 0, sizeof s_sosi);
    memset(&s_ltcf, 0, sizeof s_ltcf);
    memset(&s_ltci, 0, sizeof s_ltci);
    s_sosf_rows_seen = 0;
    s_sosi_rows_seen = 0;
}

void coeff_loader_abort(void)
{
    s_state = CL_IDLE;
}

coeff_load_state_e coeff_loader_state(void)
{
    return s_state;
}

/* line forms:
 *  "# comment"                -> ignore
 *  "w <key> <v0> <v1> ..."    -> data line
 *  "w <key.N> <count>"        -> not used; actual format is "w iirf.Nb 10"
 * Real format: "w iirf.b ...", "w iirf.Nb 10", "w iirf.a ...", "w iirf.Na 10"
 */
void coeff_loader_feed_line(const char *line)
{
    if (s_state != CL_WAITING)
    {
        return;
    }
    if (line[0] == '#' || line[0] == '\0')
    {
        return;
    }
    if (line[0] != 'w' || line[1] != ' ')
    {
        return;
    }

    char key[16];
    const char *p = line + 2;
    int ki = 0;
    while (*p && *p != ' ' && ki < 15)
    {
        key[ki++] = *p++;
    }
    key[ki] = '\0';
    while (*p == ' ')
    {
        p++;
    }

    ESP_LOGI("DBGCL", "key=[%s] p=[%s] p_len=%d", key, p, (int)strlen(p));

    bool done = false;

    switch (s_target)
    {
    case _IIR_FLOAT:
        if (!strcmp(key, "iirf.b"))
        {
            s_iirf.Nb = (uint8_t)parse_floats(p, s_iirf.b, DF_MAX_IIR_TAPS);
            s_have_primary = true;
        }
        else if (!strcmp(key, "iirf.Nb"))
        {
            s_iirf.Nb = (uint8_t)atoi(p);
            s_have_primary_n = true;
        }
        else if (!strcmp(key, "iirf.a"))
        {
            s_iirf.Na = (uint8_t)parse_floats(p, s_iirf.a, DF_MAX_IIR_TAPS);
            s_have_secondary = true;
        }
        else if (!strcmp(key, "iirf.Na"))
        {
            s_iirf.Na = (uint8_t)atoi(p);
            s_have_secondary_n = true;
        }
        break;
    case _IIR_INT:
        if (!strcmp(key, "iiri.Sb"))
        {
            s_iiri.Nb = (uint8_t)parse_ints(p, s_iiri.Sb, DF_MAX_IIR_TAPS);
            s_have_primary = true;
        }
        else if (!strcmp(key, "iiri.Nb"))
        {
            s_iiri.Nb = (uint8_t)atoi(p);
            s_have_primary_n = true;
        }
        else if (!strcmp(key, "iiri.Sa"))
        {
            s_iiri.Na = (uint8_t)parse_ints(p, s_iiri.Sa, DF_MAX_IIR_TAPS);
            s_have_secondary = true;
        }
        else if (!strcmp(key, "iiri.Na"))
        {
            s_iiri.Na = (uint8_t)atoi(p);
            s_have_secondary_n = true;
        }
        break;
    case _FIR_FLOAT:
    {
        if (strncmp(key, "firf.b", 6) == 0)
        {
            int offset = 0;
            if (key[6] == '+')
                offset = atoi(key + 7);
            int n = parse_floats(p, &s_firf.b[offset], DF_MAX_FIR_TAPS - offset);
            if (offset + n > s_firf.M)
                s_firf.M = (uint16_t)(offset + n);
            s_have_primary = true;
        }
        else if (!strcmp(key, "firf.N") || !strcmp(key, "firf.M"))
        {
            s_firf.M = (uint16_t)atoi(p);
            s_have_primary_n = true;
            s_have_secondary = true;
            s_have_secondary_n = true;
        }
        break;
    }
    case _FIR_INT:
    {
        int32_t itmp[DF_MAX_FIR_TAPS];
        if (strncmp(key, "firi.Sb", 7) == 0)
        {
            int offset = 0;
            if (key[7] == '+')
                offset = atoi(key + 8);
            int n = parse_ints(p, itmp, DF_MAX_FIR_TAPS);
            for (int i = 0; i < n; i++)
                s_firi.Sb[offset + i] = (int16_t)itmp[i];
            if (offset + n > s_firi.M)
                s_firi.M = (uint16_t)(offset + n);
            s_have_primary = true;
        }
        else if (!strcmp(key, "firi.N") || !strcmp(key, "firi.M"))
        {
            s_firi.M = (uint16_t)atoi(p);
            s_have_primary_n = true;
            s_have_secondary = true;
            s_have_secondary_n = true;
        }
        break;
    }
    case _SOS_FLOAT:
    {
        if (key[0] == 's' && key[1] == 'o' && key[2] == 's' && key[3] == 'f' &&
            key[4] >= '0' && key[4] <= '9')
        {
            int idx = atoi(key + 4);
            float v[6];
            int n = parse_floats(p, v, 6);
            if (n == 6 && idx < DF_MAX_SOS_STAGES)
            {
                memcpy(s_sosf.sos[idx], v, sizeof v);
                s_have_primary = true;
                s_sosf_rows_seen++;
            }
        }
        else if (!strcmp(key, "sosf.N"))
        {
            s_sosf.Nsos = (uint8_t)atoi(p);
            s_have_primary_n = true;
            s_have_secondary = true;
            s_have_secondary_n = true;
        }
        break;
    }
    case _SOS_INT:
    {
        if (key[0] == 's' && key[1] == 'o' && key[2] == 's' && key[3] == 'i' &&
            key[4] >= '0' && key[4] <= '9')
        {
            int idx = atoi(key + 4);
            int32_t v[6];
            int n = parse_ints(p, v, 6);
            if (n == 6 && idx < DF_MAX_SOS_STAGES)
            {
                memcpy(s_sosi.sos[idx], v, sizeof v);
                s_have_primary = true;
                s_sosi_rows_seen++;
            }
        }
        else if (!strcmp(key, "sosi.N"))
        {
            s_sosi.Nsos = (uint8_t)atoi(p);
            s_have_primary_n = true;
            s_have_secondary = true;
            s_have_secondary_n = true;
        }
        break;
    }
    case _LTC_FLOAT:
        if (!strcmp(key, "ltcf.nu"))
        {
            s_ltcf.Nnu = (uint8_t)parse_floats(p, s_ltcf.nu, DF_MAX_LTC_ORDER + 1);
            s_have_primary = true;
        }
        else if (!strcmp(key, "ltcf.Nnu"))
        {
            s_ltcf.Nnu = (uint8_t)atoi(p);
            s_have_primary_n = true;
        }
        else if (!strcmp(key, "ltcf.k"))
        {
            s_ltcf.Nk = (uint8_t)parse_floats(p, s_ltcf.k, DF_MAX_LTC_ORDER + 1);
            s_have_secondary = true;
        }
        else if (!strcmp(key, "ltcf.Nk"))
        {
            s_ltcf.Nk = (uint8_t)atoi(p);
            s_have_secondary_n = true;
        }
        break;
    case _LTC_INT:
        if (!strcmp(key, "ltci.Snu"))
        {
            s_ltci.Nnu = (uint8_t)parse_ints(p, s_ltci.Snu, DF_MAX_LTC_ORDER + 1);
            s_have_primary = true;
        }
        else if (!strcmp(key, "ltci.Nnu"))
        {
            s_ltci.Nnu = (uint8_t)atoi(p);
            s_have_primary_n = true;
        }
        else if (!strcmp(key, "ltci.Sk"))
        {
            s_ltci.Nk = (uint8_t)parse_ints(p, s_ltci.Sk, DF_MAX_LTC_ORDER + 1);
            s_have_secondary = true;
        }
        else if (!strcmp(key, "ltci.Nk"))
        {
            s_ltci.Nk = (uint8_t)atoi(p);
            s_have_secondary_n = true;
        }
        break;
    default:
        done = s_have_primary && s_have_primary_n && s_have_secondary && s_have_secondary_n;
        break;
    }

    switch (s_target)
    {
    case _SOS_FLOAT:
        done = s_have_primary_n && (s_sosf_rows_seen >= s_sosf.Nsos);
        break;
    case _SOS_INT:
        done = s_have_primary_n && (s_sosi_rows_seen >= s_sosi.Nsos);
        break;
    default:
        done = s_have_primary && s_have_primary_n && s_have_secondary && s_have_secondary_n;
        break;
    }

    if (done)
    {
        s_state = CL_DONE_OK;
    }
}

static bool validate(void)
{
    switch (s_target)
    {
    case _IIR_FLOAT:
        return s_iirf.Nb > 0 && s_iirf.Na > 0 && s_iirf.a[0] != 0.0f;
    case _IIR_INT:
        return s_iiri.Nb > 0 && s_iiri.Na > 0 && s_iiri.Sa[0] == DF_S_IIR;
    case _FIR_FLOAT:
        return s_firf.M > 0 && s_firf.M <= DF_MAX_FIR_TAPS;
    case _FIR_INT:
        return s_firi.M > 0 && s_firi.M <= DF_MAX_FIR_TAPS;
    case _SOS_FLOAT:
        return s_sosf.Nsos > 0 && s_sosf.Nsos <= DF_MAX_SOS_STAGES;
    case _SOS_INT:
        return s_sosi.Nsos > 0 && s_sosi.Nsos <= DF_MAX_SOS_STAGES;
    case _LTC_FLOAT:
        return s_ltcf.Nnu > 0 && s_ltcf.Nk > 0 && s_ltcf.Nk <= DF_MAX_LTC_ORDER && s_ltcf.Nnu <= DF_MAX_LTC_ORDER + 1;
    case _LTC_INT:
        return s_ltci.Nnu > 0 && s_ltci.Nk > 0 && s_ltci.Nk <= DF_MAX_LTC_ORDER && s_ltci.Nnu <= DF_MAX_LTC_ORDER + 1;
    default:
        return false;
    }
}

bool coeff_loader_commit(filter_type_e *out_type, const void **out_coeff)
{
    if (s_state != CL_DONE_OK || !validate())
    {
        s_state = CL_DONE_FAIL;
        return false;
    }
    *out_type = s_target;
    switch (s_target)
    {
    case _IIR_FLOAT:
        *out_coeff = &s_iirf;
        break;
    case _IIR_INT:
        *out_coeff = &s_iiri;
        break;
    case _FIR_FLOAT:
        *out_coeff = &s_firf;
        break;
    case _FIR_INT:
        *out_coeff = &s_firi;
        break;
    case _SOS_FLOAT:
        *out_coeff = &s_sosf;
        break;
    case _SOS_INT:
        *out_coeff = &s_sosi;
        break;
    case _LTC_FLOAT:
        *out_coeff = &s_ltcf;
        break;
    case _LTC_INT:
        *out_coeff = &s_ltci;
        break;
    default:
        return false;
    }
    s_state = CL_IDLE;
    return true;
}