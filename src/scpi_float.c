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

static size_t scan_number(const char *s, size_t n);

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
    if (n >= sizeof tmp || scan_number(s, n) != n) {
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

/* ---- Range-checked parsers with keywords ---------------------------------------- */

/*
 * Shared body of the range-checked double parsers. @p units NULL means no unit
 * suffix; @p kw_out NULL means UP/DOWN are not accepted (they then fail as
 * an ordinary non-number).
 */
static viper_scpi_result_t double_kw(const viper_scpi_context_t *ctx, size_t idx,
                                     const viper_scpi_unit_t *units, double min, double max,
                                     double def, double *out, viper_scpi_numeric_kw_t *kw_out)
{
    viper_scpi_numeric_kw_t kw;
    double v;
    viper_scpi_result_t res;

    if (out == NULL || !(min <= max)) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    kw = viper_scpi_numeric_keyword(ctx, idx);
    if ((kw == VIPER_SCPI_KW_UP || kw == VIPER_SCPI_KW_DOWN) && kw_out == NULL) {
        kw = VIPER_SCPI_KW_NONE;
    }
    if (kw != VIPER_SCPI_KW_NONE) {
        if (kw == VIPER_SCPI_KW_MIN) {
            *out = min;
        } else if (kw == VIPER_SCPI_KW_MAX) {
            *out = max;
        } else if (kw == VIPER_SCPI_KW_DEF) {
            *out = def;
        }
        if (kw_out != NULL) {
            *kw_out = kw;
        }
        return VIPER_SCPI_RES_OK;
    }
    res = (units != NULL) ? viper_scpi_param_double_unit(ctx, idx, units, &v)
                          : viper_scpi_param_double(ctx, idx, &v);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (v != v) {
        return VIPER_SCPI_RES_ERR_ILLEGAL_VALUE; /* NAN is never in a range */
    }
    if (v < min || v > max) {
        return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
    }
    *out = v;
    if (kw_out != NULL) {
        *kw_out = VIPER_SCPI_KW_NONE;
    }
    return VIPER_SCPI_RES_OK;
}

viper_scpi_result_t viper_scpi_param_double_kw(const viper_scpi_context_t *ctx, size_t idx,
                                               double min, double max, double def,
                                               double *out)
{
    return double_kw(ctx, idx, NULL, min, max, def, out, NULL);
}

viper_scpi_result_t viper_scpi_param_double_kw_ex(const viper_scpi_context_t *ctx, size_t idx,
                                                  double min, double max, double def,
                                                  double *out, viper_scpi_numeric_kw_t *kw)
{
    if (kw == NULL) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    return double_kw(ctx, idx, NULL, min, max, def, out, kw);
}

/* ---- Numbers with units ------------------------------------------------------------------------ */

const viper_scpi_unit_t viper_scpi_units_volt[] = { { "V", 1.0, 0 }, { NULL, 0, 0 } };
const viper_scpi_unit_t viper_scpi_units_amp[] = { { "A", 1.0, 0 }, { NULL, 0, 0 } };
const viper_scpi_unit_t viper_scpi_units_hertz[] = {
    { "HZ", 1.0, VIPER_SCPI_UNIT_M_IS_MEGA }, { NULL, 0, 0 }
};
const viper_scpi_unit_t viper_scpi_units_ohm[] = {
    { "OHM", 1.0, VIPER_SCPI_UNIT_M_IS_MEGA }, { NULL, 0, 0 }
};
const viper_scpi_unit_t viper_scpi_units_watt[] = { { "W", 1.0, 0 }, { NULL, 0, 0 } };
const viper_scpi_unit_t viper_scpi_units_farad[] = { { "F", 1.0, 0 }, { NULL, 0, 0 } };
const viper_scpi_unit_t viper_scpi_units_henry[] = { { "H", 1.0, 0 }, { NULL, 0, 0 } };
const viper_scpi_unit_t viper_scpi_units_decibel[] = { { "DB", 1.0, 0 }, { NULL, 0, 0 } };
const viper_scpi_unit_t viper_scpi_units_second[] = {
    { "S", 1.0, 0 },
    { "SEC", 1.0, 0 },
    { "MIN", 60.0, VIPER_SCPI_UNIT_NO_PREFIX },
    { "HOUR", 3600.0, VIPER_SCPI_UNIT_NO_PREFIX },
    { NULL, 0, 0 }
};
const viper_scpi_unit_t viper_scpi_units_celsius[] = {
    { "CEL", 1.0, VIPER_SCPI_UNIT_NO_PREFIX }, { NULL, 0, 0 }
};

/* Multiplier prefixes as powers of 1000. Scaling multiplies or divides by
 * an exact power of ten, so "10US" is the same double as "10E-6": an inexact
 * factor such as 1e-6 would add a second rounding and put values on a range
 * limit just outside it. */
static const struct {
    char name[3];
    int exp3;
} prefixes[] = {
    { "EX", 6 }, { "PE", 5 }, { "MA", 2 }, { "T", 4 }, { "G", 3 }, { "K", 1 },
    { "M", -1 }, { "U", -2 }, { "N", -3 }, { "P", -4 }, { "F", -5 }, { "A", -6 },
};

static const double pow1000[] = { 1e0, 1e3, 1e6, 1e9, 1e12, 1e15, 1e18 }; /* exact */

/* a[0..alen) equals the NUL-terminated b, ignoring case. */
static bool equals_nocase(const char *a, size_t alen, const char *b)
{
    for (size_t i = 0; i < alen; i++) {
        if (b[i] == '\0' || viper_scpi_upper(a[i]) != viper_scpi_upper(b[i])) {
            return false;
        }
    }
    return b[alen] == '\0';
}

/* Resolves a suffix such as "MV" or "KHZ": the unit's scale to the base
 * unit and the prefix as a power of 1000. */
static bool lookup_unit(const viper_scpi_unit_t *units, const char *sfx, size_t n,
                        double *scale, int *exp3)
{
    *exp3 = 0;
    for (const viper_scpi_unit_t *u = units; u->name != NULL; u++) {
        if (equals_nocase(sfx, n, u->name)) {
            *scale = u->scale;
            return true;
        }
    }
    for (const viper_scpi_unit_t *u = units; u->name != NULL; u++) {
        if ((u->flags & VIPER_SCPI_UNIT_NO_PREFIX) != 0) {
            continue;
        }
        for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++) {
            size_t pl = prefixes[i].name[1] != '\0' ? 2u : 1u; /* "EX" or "K" */

            if (n <= pl || !equals_nocase(sfx, pl, prefixes[i].name) ||
                !equals_nocase(sfx + pl, n - pl, u->name)) {
                continue;
            }
            *exp3 = prefixes[i].exp3;
            if (pl == 1 && prefixes[i].name[0] == 'M' &&
                (u->flags & VIPER_SCPI_UNIT_M_IS_MEGA) != 0) {
                *exp3 = 2;
            }
            *scale = u->scale;
            return true;
        }
    }
    return false;
}

/* Length of the leading number: [sign] digits [. digits] [E [sign] digits]. */
static size_t scan_number(const char *s, size_t n)
{
    size_t i = 0;
    size_t digits = 0;

    if (i < n && (s[i] == '+' || s[i] == '-')) {
        i++;
    }
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        i++;
        digits++;
    }
    if (i < n && s[i] == '.') {
        i++;
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            i++;
            digits++;
        }
    }
    if (digits == 0) {
        return 0;
    }
    /* An exponent needs digits; otherwise the 'E' starts a unit (EX = exa). */
    if (i < n && viper_scpi_upper(s[i]) == 'E') {
        size_t j = i + 1;
        if (j < n && (s[j] == '+' || s[j] == '-')) {
            j++;
        }
        if (j < n && s[j] >= '0' && s[j] <= '9') {
            while (j < n && s[j] >= '0' && s[j] <= '9') {
                j++;
            }
            i = j;
        }
    }
    return i;
}

viper_scpi_result_t viper_scpi_param_double_unit(const viper_scpi_context_t *ctx, size_t idx,
                                                 const viper_scpi_unit_t *units, double *out)
{
    const char *s;
    size_t n;
    size_t num;
    char tmp[48];
    double v;
    viper_scpi_result_t res;

    if (units == NULL || out == NULL) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    res = viper_scpi_param_raw(ctx, idx, &s, &n);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (special_double(s, n, out)) {
        return VIPER_SCPI_RES_OK;
    }
    num = scan_number(s, n);
    if (num == 0 || num >= sizeof tmp) {
        return VIPER_SCPI_RES_ERR_DATA_TYPE;
    }
    memcpy(tmp, s, num);
    tmp[num] = '\0';
    if (!to_double(tmp, num, &v)) {
        return VIPER_SCPI_RES_ERR_DATA_TYPE;
    }

    while (num < n && viper_scpi_is_space(s[num])) {
        num++;
    }
    if (num < n) {
        for (size_t i = num; i < n; i++) {
            char c = viper_scpi_upper(s[i]);
            if (c < 'A' || c > 'Z') {
                return VIPER_SCPI_RES_ERR_INVALID_SUFFIX;
            }
        }
        double scale;
        int exp3;

        if (!lookup_unit(units, s + num, n - num, &scale, &exp3)) {
            return VIPER_SCPI_RES_ERR_INVALID_SUFFIX;
        }
        v = (exp3 >= 0) ? v * pow1000[exp3] : v / pow1000[-exp3];
        v *= scale;
    }
    if (v > DBL_MAX || v < -DBL_MAX) {
        return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
    }
    *out = v;
    return VIPER_SCPI_RES_OK;
}

viper_scpi_result_t viper_scpi_param_double_unit_kw(const viper_scpi_context_t *ctx, size_t idx,
                                                    const viper_scpi_unit_t *units,
                                                    double min, double max, double def,
                                                    double *out)
{
    if (units == NULL) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    return double_kw(ctx, idx, units, min, max, def, out, NULL);
}

viper_scpi_result_t viper_scpi_param_double_unit_kw_ex(const viper_scpi_context_t *ctx,
                                                       size_t idx, const viper_scpi_unit_t *units,
                                                       double min, double max, double def,
                                                       double *out, viper_scpi_numeric_kw_t *kw)
{
    if (units == NULL || kw == NULL) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    return double_kw(ctx, idx, units, min, max, def, out, kw);
}
