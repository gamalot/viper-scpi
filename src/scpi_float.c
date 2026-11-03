/*
 * Floating-point parameters and responses. Kept in a translation unit of its
 * own: it is the only one that uses strtod() and snprintf(), so firmware that
 * never calls a double API does not link them -- nor the printf, dtoa and
 * soft-float code behind them -- from the static library.
 */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scpi_internal.h"

/* avr-libc's math.h has INFINITY but not HUGE_VAL; C99 has both. */
#ifdef INFINITY
#define VIPER_INF ((double)INFINITY)
#else
#define VIPER_INF HUGE_VAL
#endif

/* ---- Classification ---------------------------------------------------------- */

enum { NUM_FINITE, NUM_INF, NUM_NAN };

/*
 * INF and NAN from the bit pattern: comparisons such as v != v are folded
 * away under -ffast-math / -ffinite-math-only, common in firmware builds.
 */
static int classify(double v)
{
#if DBL_MANT_DIG == 53
    uint64_t bits;

    if (sizeof bits == sizeof v) {
        memcpy(&bits, &v, sizeof bits);
        if ((bits & UINT64_C(0x7FF0000000000000)) != UINT64_C(0x7FF0000000000000)) {
            return NUM_FINITE;
        }
        return (bits & UINT64_C(0x000FFFFFFFFFFFFF)) != 0 ? NUM_NAN : NUM_INF;
    }
#elif DBL_MANT_DIG == 24
    uint32_t bits;

    if (sizeof bits == sizeof v) { /* double is binary32 (avr-gcc) */
        memcpy(&bits, &v, sizeof bits);
        if ((bits & 0x7F800000u) != 0x7F800000u) {
            return NUM_FINITE;
        }
        return (bits & 0x007FFFFFu) != 0 ? NUM_NAN : NUM_INF;
    }
#endif
    if (v != v) {
        return NUM_NAN;
    }
    return (v > DBL_MAX || v < -DBL_MAX) ? NUM_INF : NUM_FINITE;
}

/* ---- Responses -------------------------------------------------------------- */

/* The most significant digits a response may have: enough to return every
 * double unchanged, more only adds noise (17 for binary64, 9 for binary32). */
#define MAX_DIGITS ((DBL_MANT_DIG) > 24 ? 17 : 9)

void viper_scpi_result_double_digits(viper_scpi_context_t *ctx, double value, int digits)
{
    char buf[40];
    const char *s = buf;
    int kind = classify(value);

    if (digits < 1) {
        digits = 1;
    } else if (digits > MAX_DIGITS) {
        digits = MAX_DIGITS;
    }
    /* SCPI defines 9.9E37 as infinity and 9.91E37 as "not a number". */
    if (kind == NUM_NAN) {
        s = "9.91E+37";
    } else if (kind == NUM_INF) {
        s = (value < 0) ? "-9.9E+37" : "9.9E+37";
    } else {
        /* IEEE 488.2 8.7.4: the NR3 exponent marker is an upper-case E. */
        size_t w = 0;
        bool digit = false;

        (void)snprintf(buf, sizeof buf, "%.*G", digits, value);
        /* The radix character follows LC_NUMERIC and may be several bytes;
         * SCPI's is always '.' (a ',' would read as a data separator). */
        for (size_t r = 0; buf[r] != '\0'; r++) {
            char c = buf[r];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == 'E') {
                buf[w++] = c;
                digit = digit || (c >= '0' && c <= '9');
            } else if (w == 0 || buf[w - 1] != '.') {
                buf[w++] = '.';
            }
        }
        buf[w] = '\0';
        if (!digit) {
            /* A C library without floating-point printf (newlib-nano without
             * -u _printf_float, avr-libc's default vfprintf). */
            viper_scpi_error_push_info(ctx, VIPER_SCPI_RES_ERR_EXECUTION,
                                       "printf has no floating-point support");
            s = "9.91E+37";
        }
    }
    viper_scpi_result_text(ctx, s);
}

void viper_scpi_result_double(viper_scpi_context_t *ctx, double value)
{
    viper_scpi_result_double_digits(ctx, value,
                                    (VIPER_SCPI_DOUBLE_DIGITS) > 0 ? (VIPER_SCPI_DOUBLE_DIGITS)
                                                                   : DBL_DIG);
}

/* ---- Parameters ---------------------------------------------------------------- */

/* Significant digits kept for strtod(); further ones are folded into one
 * sticky digit. Up to this many digits a number is rounded exactly to the
 * nearest double; beyond, as if cut to them (only a value within 10^-40 of a
 * halfway point between two doubles can tell the difference). */
#define KEPT_DIGITS 40

/*
 * The decimal number s[0..n) (as viper_scpi_scan_number() accepts it), times
 * 10^(3 * exp3), into @p dst as "[-]<digits>E<exponent>": no radix character
 * (so LC_NUMERIC does not matter), no length limit (IEEE 488.2 7.7.2 asks for
 * long mantissas), and the multiplier prefix applied to the exponent, so that
 * strtod() rounds once ("10US" is exactly 10E-6). @p dst holds 64 bytes.
 */
static void canonical_number(const char *s, size_t n, int exp3, char *dst)
{
    size_t i = 0;
    size_t w = 0;
    size_t kept = 0;
    long dexp = 3L * exp3; /* value = <kept digits> * 10^(dexp + exponent) */
    long e = 0;
    bool point = false;
    bool sticky = false;
    bool eneg = false;

    if (i < n && (s[i] == '+' || s[i] == '-')) {
        if (s[i] == '-') {
            dst[w++] = '-';
        }
        i++;
    }
    for (; i < n; i++) {
        char c = s[i];
        if (c == '.') {
            point = true;
            continue;
        }
        if (c < '0' || c > '9') {
            break;
        }
        if (kept == 0 && c == '0') {
            dexp -= point ? 1 : 0; /* leading zero */
        } else if (kept < KEPT_DIGITS) {
            dst[w++] = c;
            kept++;
            dexp -= point ? 1 : 0;
        } else {
            sticky = sticky || c != '0';
            dexp += point ? 0 : 1; /* a dropped integer digit */
        }
    }
    if (kept == 0) {
        dst[w++] = '0';
    }
    if (sticky) {
        dst[w++] = '1'; /* nonzero digits follow the kept ones */
        dexp--;
    }
    while (i < n && (viper_scpi_is_space(s[i]) || viper_scpi_upper(s[i]) == 'E')) {
        i++;
    }
    if (i < n && (s[i] == '+' || s[i] == '-')) {
        eneg = s[i] == '-';
        i++;
    }
    for (; i < n && s[i] >= '0' && s[i] <= '9'; i++) {
        if (e < 100000) {
            e = e * 10 + (s[i] - '0'); /* saturates far beyond any double */
        }
    }
    e = (eneg ? -e : e) + dexp;
    dst[w++] = 'E';
    if (e < 0) {
        dst[w++] = '-';
        e = -e;
    }
    {
        char digits[8];
        size_t k = 0;
        do {
            digits[k++] = (char)('0' + e % 10);
            e /= 10;
        } while (e != 0);
        while (k > 0) {
            dst[w++] = digits[--k];
        }
    }
    dst[w] = '\0';
}

/* INFinity, NINFinity and NAN (SCPI-99 7.2.1.5). */
static bool special_double(const char *s, size_t n, double *v)
{
    if (viper_scpi_pattern_match("INFinity", s, n)) {
        *v = VIPER_INF;
    } else if (viper_scpi_pattern_match("NINFinity", s, n)) {
        *v = -VIPER_INF;
    } else if (viper_scpi_pattern_match("NAN", s, n)) {
#ifdef NAN
        *v = NAN;
#else
        *v = VIPER_INF - VIPER_INF;
#endif
    } else {
        return false;
    }
    return true;
}

/* A number, with a unit of @p units if given (NULL: a bare number only), in
 * the base unit. */
static viper_scpi_result_t parse_double(const viper_scpi_context_t *ctx, size_t idx,
                                        const viper_scpi_unit_table_t *units, double *out)
{
    const char *s;
    size_t n;
    size_t num;
    const viper_scpi_unit_t *unit = NULL;
    int exp3 = 0;
    char tmp[64];
    double v;
    viper_scpi_result_t res = viper_scpi_param_raw(ctx, idx, &s, &n);

    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (special_double(s, n, out)) {
        return VIPER_SCPI_RES_OK;
    }
    if (units != NULL) {
        res = units->split(units->list, s, n, &num, &unit, &exp3);
        if (res != VIPER_SCPI_RES_OK) {
            return res;
        }
    } else {
        /* The whole token must be a number (scan_number() stops before a unit). */
        num = viper_scpi_scan_number(s, n);
        if (num == 0 || num != n) {
            return VIPER_SCPI_RES_ERR_DATA_TYPE;
        }
    }
    canonical_number(s, num, exp3, tmp);
    v = strtod(tmp, NULL);
    if (unit != NULL) {
        v *= unit->scale;
    }
    if (classify(v) != NUM_FINITE) {
        return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
    }
    *out = v;
    return VIPER_SCPI_RES_OK;
}

viper_scpi_result_t viper_scpi_param_double(const viper_scpi_context_t *ctx, size_t idx,
                                            double *out)
{
    if (out == NULL) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    return parse_double(ctx, idx, NULL, out);
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

    if (spec == NULL || out == NULL || !(spec->min <= spec->max) ||
        (spec->units != NULL && (spec->units->split == NULL || spec->units->list == NULL))) {
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
    res = parse_double(ctx, idx, spec->units, &v);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (classify(v) == NUM_NAN) {
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
