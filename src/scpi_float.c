/*
 * Floating-point parameters and responses. Kept in a translation unit of its
 * own: it is the only one that uses strtod() and snprintf(), so firmware that
 * never calls a double API does not link them -- nor the printf, dtoa and
 * soft-float code behind them -- from the static library.
 */
#include <float.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scpi_internal.h"

/* ---- Responses -------------------------------------------------------------- */

void viper_scpi_result_double_digits(viper_scpi_context_t *ctx, double value, int digits)
{
    char buf[40];
    const char *s = buf;

    if (digits < 1) {
        digits = 1;
    } else if (digits > 17) {
        digits = 17;
    }
    /* SCPI defines 9.9E37 as infinity and 9.91E37 as "not a number". */
    if (value != value) {
        s = "9.91E+37";
    } else if (value > DBL_MAX) {
        s = "9.9E+37";
    } else if (value < -DBL_MAX) {
        s = "-9.9E+37";
    } else {
        /* IEEE 488.2 8.7.4: the NR3 exponent marker is an upper-case E. */
        (void)snprintf(buf, sizeof buf, "%.*G", digits, value);
        /* The radix character follows LC_NUMERIC; SCPI's is always '.' (a
         * ',' would read as a data separator). Everything else %G writes is
         * a digit, a sign or 'E'. */
        for (char *p = buf; *p != '\0'; p++) {
            if (!(*p >= '0' && *p <= '9') && *p != '-' && *p != '+' && *p != 'E') {
                *p = '.';
            }
        }
    }
    viper_scpi_result_text(ctx, s);
}

void viper_scpi_result_double(viper_scpi_context_t *ctx, double value)
{
    viper_scpi_result_double_digits(ctx, value, VIPER_SCPI_DOUBLE_DIGITS);
}

/* ---- Parameters ---------------------------------------------------------------- */

/*
 * strtod() of the NUL-terminated number text @p tmp (@p n characters, already
 * validated). strtod() follows LC_NUMERIC, so in a locale whose radix is not
 * '.' (a hosted build after setlocale()) the '.' is swapped for it. False if
 * the text still is not read completely.
 */
static bool to_double(char *tmp, size_t n, double *v)
{
    char *end;

    *v = strtod(tmp, &end);
    if (end != tmp + n) {
        const char *dp = localeconv()->decimal_point;
        char *point = strchr(tmp, '.');

        if (point == NULL || dp == NULL || dp[0] == '\0' || dp[0] == '.' || dp[1] != '\0') {
            return false;
        }
        *point = dp[0];
        *v = strtod(tmp, &end);
    }
    return end == tmp + n;
}

/* INFinity, NINFinity and NAN (SCPI-99 7.2.1.5). */
static bool special_double(const char *s, size_t n, double *v)
{
    if (viper_scpi_pattern_match("INFinity", s, n)) {
        *v = HUGE_VAL;
    } else if (viper_scpi_pattern_match("NINFinity", s, n)) {
        *v = -HUGE_VAL;
    } else if (viper_scpi_pattern_match("NAN", s, n)) {
#ifdef NAN
        *v = NAN;
#else
        *v = HUGE_VAL - HUGE_VAL;
#endif
    } else {
        return false;
    }
    return true;
}

viper_scpi_result_t viper_scpi_param_double(const viper_scpi_context_t *ctx, size_t idx,
                                            double *out)
{
    const char *s;
    size_t n;
    char tmp[48];
    double v;
    viper_scpi_result_t res;

    if (out == NULL) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    res = viper_scpi_param_raw(ctx, idx, &s, &n);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (special_double(s, n, out)) {
        return VIPER_SCPI_RES_OK;
    }
    /* The whole token must be a number (scan_number() stops before a unit). */
    if (n >= sizeof tmp || viper_scpi_scan_number(s, n) != n) {
        return VIPER_SCPI_RES_ERR_DATA_TYPE;
    }
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    if (!to_double(tmp, n, &v)) {
        return VIPER_SCPI_RES_ERR_DATA_TYPE;
    }
    if (v > DBL_MAX || v < -DBL_MAX) {
        return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
    }
    *out = v;
    return VIPER_SCPI_RES_OK;
}

/* ---- Numbers with units ------------------------------------------------------------------------ */

static const double pow1000[] = { 1e0, 1e3, 1e6, 1e9, 1e12, 1e15, 1e18 }; /* exact */

/* A number with an optional unit of @p units, in the base unit. */
static viper_scpi_result_t double_unit(const viper_scpi_context_t *ctx, size_t idx,
                                       const viper_scpi_unit_t *units, double *out)
{
    const char *s;
    size_t n;
    size_t num;
    char tmp[48];
    double v;
    const viper_scpi_unit_t *unit;
    int exp3;
    viper_scpi_result_t res = viper_scpi_param_raw(ctx, idx, &s, &n);

    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (special_double(s, n, out)) {
        return VIPER_SCPI_RES_OK;
    }
    res = viper_scpi_number_unit(units, s, n, &num, &unit, &exp3);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (num >= sizeof tmp) {
        return VIPER_SCPI_RES_ERR_DATA_TYPE;
    }
    memcpy(tmp, s, num);
    tmp[num] = '\0';
    if (!to_double(tmp, num, &v)) {
        return VIPER_SCPI_RES_ERR_DATA_TYPE;
    }
    if (unit != NULL) {
        v = (exp3 >= 0) ? v * pow1000[exp3] : v / pow1000[-exp3];
        v *= unit->scale;
    }
    if (v > DBL_MAX || v < -DBL_MAX) {
        return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
    }
    *out = v;
    return VIPER_SCPI_RES_OK;
}

/* ---- The range-checked parser ------------------------------------------------------------------ */

viper_scpi_result_t viper_scpi_param_number(const viper_scpi_context_t *ctx, size_t idx,
                                            const viper_scpi_number_t *spec, double *out,
                                            viper_scpi_numeric_kw_t *kw_out)
{
    viper_scpi_numeric_kw_t kw;
    double v;
    viper_scpi_result_t res;
    bool updown;

    if (spec == NULL || out == NULL || !(spec->min <= spec->max)) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    updown = (spec->flags & VIPER_SCPI_NUM_UPDOWN) != 0;
    if (updown && kw_out == NULL) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID; /* UP/DOWN must be reported */
    }
    kw = viper_scpi_numeric_keyword(ctx, idx);
    if ((kw == VIPER_SCPI_KW_UP || kw == VIPER_SCPI_KW_DOWN) && !updown) {
        kw = VIPER_SCPI_KW_NONE;
    }
    if (kw != VIPER_SCPI_KW_NONE) {
        if (kw == VIPER_SCPI_KW_MIN) {
            *out = spec->min;
        } else if (kw == VIPER_SCPI_KW_MAX) {
            *out = spec->max;
        } else if (kw == VIPER_SCPI_KW_DEF) {
            *out = spec->def;
        }
        if (kw_out != NULL) {
            *kw_out = kw;
        }
        return VIPER_SCPI_RES_OK;
    }
    res = (spec->units != NULL) ? double_unit(ctx, idx, spec->units, &v)
                                : viper_scpi_param_double(ctx, idx, &v);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (v != v) {
        return VIPER_SCPI_RES_ERR_ILLEGAL_VALUE; /* NAN is never in a range */
    }
    if (v < spec->min || v > spec->max) {
        return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
    }
    *out = v;
    if (kw_out != NULL) {
        *kw_out = VIPER_SCPI_KW_NONE;
    }
    return VIPER_SCPI_RES_OK;
}
