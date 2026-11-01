/* Parameter extraction helpers and keyword matching. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "scpi_internal.h"

/* Long/short form match of a token against a name of @p nlen characters. */
static bool match_name(const char *name, size_t nlen, const char *tok, size_t len)
{
    size_t sh = 0;

    /* Short form: the leading run of characters that are not lower case. */
    while (sh < nlen && !(name[sh] >= 'a' && name[sh] <= 'z')) {
        sh++;
    }
    if (len == 0 || (len != sh && len != nlen)) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (viper_scpi_upper(name[i]) != viper_scpi_upper(tok[i])) {
            return false;
        }
    }
    return true;
}

bool viper_scpi_pattern_match(const char *pattern, const char *tok, size_t len)
{
    return match_name(pattern, strlen(pattern), tok, len);
}

int viper_scpi_pattern_next(const char **p, viper_scpi_pattern_item_t *item)
{
    const char *q = *p;
    const char *name;

    while (*q == ':') {
        q++;
    }
    if (*q == '\0') {
        return 0;
    }
    item->optional = false;
    if (*q == '[') {
        q++;
        while (*q == ':') {
            q++;
        }
        name = q;
        while (*q != '\0' && *q != ']' && *q != ':' && *q != '[') {
            q++;
        }
        if (*q != ']') {
            return -1;
        }
        item->optional = true;
        item->len = (size_t)(q - name);
        q++;
    } else {
        name = q;
        while (*q != '\0' && *q != ':' && *q != '[' && *q != ']') {
            q++;
        }
        item->len = (size_t)(q - name);
    }
    item->has_suffix = item->len > 0 && name[item->len - 1] == '#';
    if (item->has_suffix) {
        item->len--;
    }
    if (item->len == 0) {
        return -1;
    }
    item->name = name;
    *p = q;
    return 1;
}

bool viper_scpi_pattern_all_optional(const char *pattern)
{
    viper_scpi_pattern_item_t it;
    int r;

    while ((r = viper_scpi_pattern_next(&pattern, &it)) == 1) {
        if (!it.optional) {
            return false;
        }
    }
    return r == 0;
}

bool viper_scpi_item_match(const viper_scpi_pattern_item_t *item, const char *tok,
                           size_t len, int32_t *suffix)
{
    *suffix = -1;
    if (item->has_suffix) {
        size_t d = 0;
        int32_t v = 0;

        while (d < len && tok[len - 1 - d] >= '0' && tok[len - 1 - d] <= '9') {
            d++;
        }
        if (d > 9 || d == len) {
            return false;
        }
        for (size_t i = len - d; i < len; i++) {
            v = v * 10 + (tok[i] - '0');
        }
        if (d > 0) {
            *suffix = v;
        }
        len -= d;
    }
    return match_name(item->name, item->len, tok, len);
}

int32_t viper_scpi_suffix(const viper_scpi_context_t *ctx, size_t index, int32_t default_value)
{
    if (index >= ctx->suffix_count || ctx->suffix[index] < 0) {
        return default_value;
    }
    return ctx->suffix[index];
}

size_t viper_scpi_block_span(const char *p, size_t n)
{
    size_t digits;
    size_t length = 0;

    if (n < 2 || p[0] != '#') {
        return 0;
    }
    if (p[1] == '0') {
        return n; /* indefinite length: runs to the end of the line */
    }
    if (p[1] < '1' || p[1] > '9') {
        return 0;
    }
    digits = (size_t)(p[1] - '0');
    if (n < 2 + digits) {
        return 0;
    }
    for (size_t i = 0; i < digits; i++) {
        char c = p[2 + i];
        if (c < '0' || c > '9') {
            return 0;
        }
        length = length * 10 + (size_t)(c - '0');
    }
    if (length > n - 2 - digits) {
        return 0;
    }
    return 2 + digits + length;
}

size_t viper_scpi_param_count(const viper_scpi_context_t *ctx)
{
    return ctx->param_count;
}

bool viper_scpi_param_present(const viper_scpi_context_t *ctx, size_t idx)
{
    return idx < ctx->param_count;
}

viper_scpi_result_t viper_scpi_param_raw(const viper_scpi_context_t *ctx, size_t idx,
                                         const char **str, size_t *len)
{
    if (str == NULL || len == NULL) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    if (idx >= ctx->param_count) {
        return VIPER_SCPI_RES_ERR_MISSING_PARAM;
    }
    *str = ctx->params[idx].str;
    *len = ctx->params[idx].len;
    return VIPER_SCPI_RES_OK;
}

/* ---- Integers ------------------------------------------------------------------ */

static int digit_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = viper_scpi_upper(c);
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Parses sign and magnitude; the magnitude saturates only via OUT_OF_RANGE. */
static viper_scpi_result_t parse_integer(const char *s, size_t n, bool *neg, uint64_t *mag)
{
    unsigned base = 10;
    uint64_t acc = 0;
    size_t i = 0;

    *neg = false;
    if (n > 0 && s[0] == '#') {
        if (n < 3) {
            return VIPER_SCPI_RES_ERR_DATA_TYPE;
        }
        switch (viper_scpi_upper(s[1])) {
        case 'H': base = 16; break;
        case 'B': base = 2; break;
        case 'Q': base = 8; break;
        default: return VIPER_SCPI_RES_ERR_DATA_TYPE;
        }
        i = 2;
    } else {
        if (n > 0 && (s[0] == '+' || s[0] == '-')) {
            *neg = (s[0] == '-');
            i = 1;
        }
        if (n - i > 2 && s[i] == '0' && viper_scpi_upper(s[i + 1]) == 'X') {
            base = 16;
            i += 2;
        }
    }
    if (i >= n) {
        return VIPER_SCPI_RES_ERR_DATA_TYPE;
    }
    for (; i < n; i++) {
        int d = digit_value(s[i]);
        if (d < 0 || (unsigned)d >= base) {
            return VIPER_SCPI_RES_ERR_DATA_TYPE;
        }
        if (acc > (UINT64_MAX - (uint64_t)d) / base) {
            return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
        }
        acc = acc * base + (uint64_t)d;
    }
    *mag = acc;
    return VIPER_SCPI_RES_OK;
}

viper_scpi_result_t viper_scpi_param_int32(const viper_scpi_context_t *ctx, size_t idx,
                                           int32_t *out)
{
    const char *s;
    size_t n;
    bool neg;
    uint64_t mag;
    viper_scpi_result_t res;

    if (out == NULL) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    res = viper_scpi_param_raw(ctx, idx, &s, &n);
    if (res == VIPER_SCPI_RES_OK) {
        res = parse_integer(s, n, &neg, &mag);
    }
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (neg ? mag > (uint64_t)INT32_MAX + 1u : mag > (uint64_t)INT32_MAX) {
        return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
    }
    *out = neg ? (int32_t)(-(int64_t)mag) : (int32_t)mag;
    return VIPER_SCPI_RES_OK;
}

viper_scpi_result_t viper_scpi_param_uint32(const viper_scpi_context_t *ctx, size_t idx,
                                            uint32_t *out)
{
    const char *s;
    size_t n;
    bool neg;
    uint64_t mag;
    viper_scpi_result_t res;

    if (out == NULL) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    res = viper_scpi_param_raw(ctx, idx, &s, &n);
    if (res == VIPER_SCPI_RES_OK) {
        res = parse_integer(s, n, &neg, &mag);
    }
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if ((neg && mag != 0) || mag > UINT32_MAX) {
        return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
    }
    *out = (uint32_t)mag;
    return VIPER_SCPI_RES_OK;
}

/* ---- Floating point ---------------------------------------------------------------- */

/* Accepts [+-] digits [. digits] [E [+-] digits] with at least one mantissa digit. */
static bool valid_float(const char *s, size_t n)
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
        return false;
    }
    if (i < n && viper_scpi_upper(s[i]) == 'E') {
        size_t exp_digits = 0;
        i++;
        if (i < n && (s[i] == '+' || s[i] == '-')) {
            i++;
        }
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            i++;
            exp_digits++;
        }
        if (exp_digits == 0) {
            return false;
        }
    }
    return i == n;
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
    if (n >= sizeof tmp || !valid_float(s, n)) {
        return VIPER_SCPI_RES_ERR_DATA_TYPE;
    }
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    v = strtod(tmp, NULL);
    if (v > 1.7976931348623157e308 || v < -1.7976931348623157e308) {
        return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
    }
    *out = v;
    return VIPER_SCPI_RES_OK;
}

/* ---- Boolean, choice, string ----------------------------------------------------------- */

viper_scpi_result_t viper_scpi_param_bool(const viper_scpi_context_t *ctx, size_t idx,
                                          bool *out)
{
    const char *s;
    size_t n;
    viper_scpi_result_t res;

    if (out == NULL) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    res = viper_scpi_param_raw(ctx, idx, &s, &n);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if ((n == 2 && viper_scpi_pattern_match("ON", s, n)) || (n == 1 && s[0] == '1')) {
        *out = true;
    } else if ((n == 3 && viper_scpi_pattern_match("OFF", s, n)) || (n == 1 && s[0] == '0')) {
        *out = false;
    } else {
        return VIPER_SCPI_RES_ERR_DATA_TYPE;
    }
    return VIPER_SCPI_RES_OK;
}

viper_scpi_result_t viper_scpi_param_choice(const viper_scpi_context_t *ctx, size_t idx,
                                            const viper_scpi_choice_t *choices, int *out)
{
    const char *s;
    size_t n;
    viper_scpi_result_t res;

    if (choices == NULL || out == NULL) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    res = viper_scpi_param_raw(ctx, idx, &s, &n);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    for (; choices->name != NULL; choices++) {
        if (viper_scpi_pattern_match(choices->name, s, n)) {
            *out = choices->value;
            return VIPER_SCPI_RES_OK;
        }
    }
    return VIPER_SCPI_RES_ERR_ILLEGAL_VALUE;
}

viper_scpi_result_t viper_scpi_param_string(const viper_scpi_context_t *ctx, size_t idx,
                                            char *out, size_t out_size, size_t *out_len)
{
    const char *s;
    size_t n;
    size_t w = 0;
    char q;
    viper_scpi_result_t res;

    if (out == NULL || out_size == 0) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    res = viper_scpi_param_raw(ctx, idx, &s, &n);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    q = s[0];
    if (n < 2 || (q != '"' && q != '\'') || s[n - 1] != q) {
        return VIPER_SCPI_RES_ERR_DATA_TYPE;
    }
    for (size_t i = 1; i < n - 1; i++) {
        if (s[i] == q) {
            if (i + 1 < n - 1 && s[i + 1] == q) {
                i++; /* doubled delimiter stands for one */
            } else {
                return VIPER_SCPI_RES_ERR_DATA_TYPE; /* stray delimiter */
            }
        }
        if (w + 1 >= out_size) {
            return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
        }
        out[w++] = s[i];
    }
    out[w] = '\0';
    if (out_len != NULL) {
        *out_len = w;
    }
    return VIPER_SCPI_RES_OK;
}

/* ---- MIN / MAX / DEF / UP / DOWN keywords ---------------------------------------------- */

static viper_scpi_numeric_kw_t numeric_keyword(const viper_scpi_context_t *ctx, size_t idx)
{
    static const struct {
        const char *name;
        viper_scpi_numeric_kw_t kw;
    } keywords[] = {
        { "MINimum", VIPER_SCPI_KW_MIN }, { "MAXimum", VIPER_SCPI_KW_MAX },
        { "DEFault", VIPER_SCPI_KW_DEF }, { "UP", VIPER_SCPI_KW_UP },
        { "DOWN", VIPER_SCPI_KW_DOWN },
    };
    const char *s;
    size_t n;

    if (viper_scpi_param_raw(ctx, idx, &s, &n) == VIPER_SCPI_RES_OK) {
        for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; i++) {
            if (viper_scpi_pattern_match(keywords[i].name, s, n)) {
                return keywords[i].kw;
            }
        }
    }
    return VIPER_SCPI_KW_NONE;
}

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
    kw = numeric_keyword(ctx, idx);
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

static viper_scpi_result_t int32_kw(const viper_scpi_context_t *ctx, size_t idx, int32_t min,
                                    int32_t max, int32_t def, int32_t *out,
                                    viper_scpi_numeric_kw_t *kw_out)
{
    viper_scpi_numeric_kw_t kw;
    int32_t v;
    viper_scpi_result_t res;

    if (out == NULL || min > max) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    kw = numeric_keyword(ctx, idx);
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
    res = viper_scpi_param_int32(ctx, idx, &v);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
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

viper_scpi_result_t viper_scpi_param_int32_kw(const viper_scpi_context_t *ctx, size_t idx,
                                              int32_t min, int32_t max, int32_t def,
                                              int32_t *out)
{
    return int32_kw(ctx, idx, min, max, def, out, NULL);
}

viper_scpi_result_t viper_scpi_param_int32_kw_ex(const viper_scpi_context_t *ctx, size_t idx,
                                                 int32_t min, int32_t max, int32_t def,
                                                 int32_t *out, viper_scpi_numeric_kw_t *kw)
{
    if (kw == NULL) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    return int32_kw(ctx, idx, min, max, def, out, kw);
}

/* ---- Block data ---------------------------------------------------------------------------- */

viper_scpi_result_t viper_scpi_param_block(const viper_scpi_context_t *ctx, size_t idx,
                                           const uint8_t **data, size_t *len)
{
    const char *s;
    size_t n;
    size_t header;
    viper_scpi_result_t res;

    if (data == NULL || len == NULL) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    res = viper_scpi_param_raw(ctx, idx, &s, &n);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (viper_scpi_block_span(s, n) != n) {
        return VIPER_SCPI_RES_ERR_DATA_TYPE;
    }
    header = (s[1] == '0') ? 2u : 2u + (size_t)(s[1] - '0');
    *data = (const uint8_t *)(s + header);
    *len = n - header;
    return VIPER_SCPI_RES_OK;
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

static const struct {
    const char *name;
    double factor;
} prefixes[] = {
    { "EX", 1e18 }, { "PE", 1e15 }, { "MA", 1e6 }, { "T", 1e12 }, { "G", 1e9 },
    { "K", 1e3 }, { "M", 1e-3 }, { "U", 1e-6 }, { "N", 1e-9 }, { "P", 1e-12 },
    { "F", 1e-15 }, { "A", 1e-18 },
};

static bool equals_nocase(const char *a, size_t alen, const char *b)
{
    if (strlen(b) != alen) {
        return false;
    }
    for (size_t i = 0; i < alen; i++) {
        if (viper_scpi_upper(a[i]) != viper_scpi_upper(b[i])) {
            return false;
        }
    }
    return true;
}

/* Resolves a suffix such as "MV" or "KHZ" to a factor to the base unit. */
static bool lookup_unit(const viper_scpi_unit_t *units, const char *sfx, size_t n,
                        double *factor)
{
    for (const viper_scpi_unit_t *u = units; u->name != NULL; u++) {
        if (equals_nocase(sfx, n, u->name)) {
            *factor = u->scale;
            return true;
        }
    }
    for (const viper_scpi_unit_t *u = units; u->name != NULL; u++) {
        if ((u->flags & VIPER_SCPI_UNIT_NO_PREFIX) != 0) {
            continue;
        }
        for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++) {
            size_t pl = strlen(prefixes[i].name);
            double f = prefixes[i].factor;

            if (n <= pl || !equals_nocase(sfx, pl, prefixes[i].name) ||
                !equals_nocase(sfx + pl, n - pl, u->name)) {
                continue;
            }
            if (pl == 1 && prefixes[i].name[0] == 'M' &&
                (u->flags & VIPER_SCPI_UNIT_M_IS_MEGA) != 0) {
                f = 1e6;
            }
            *factor = f * u->scale;
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
    double factor = 1.0;
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
    v = strtod(tmp, NULL);

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
        if (!lookup_unit(units, s + num, n - num, &factor)) {
            return VIPER_SCPI_RES_ERR_INVALID_SUFFIX;
        }
        v *= factor;
    }
    if (v > 1.7976931348623157e308 || v < -1.7976931348623157e308) {
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
