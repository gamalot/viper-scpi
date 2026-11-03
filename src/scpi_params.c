/* Parameter extraction helpers and keyword matching. */
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

/* Same rule as match_name() for a NUL-terminated pattern, without a strlen():
 * the token must equal the whole pattern, or exactly its leading run of
 * characters that are not lower case. */
bool viper_scpi_pattern_match(const char *pattern, const char *tok, size_t len)
{
    if (len == 0) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (pattern[i] == '\0' || viper_scpi_upper(pattern[i]) != viper_scpi_upper(tok[i])) {
            return false;
        }
    }
    if (pattern[len] == '\0') {
        return true; /* long form */
    }
    if (!(pattern[len] >= 'a' && pattern[len] <= 'z')) {
        return false; /* stops inside the short form */
    }
    for (size_t i = 0; i < len; i++) {
        if (pattern[i] >= 'a' && pattern[i] <= 'z') {
            return false; /* longer than the short form */
        }
    }
    return true;
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
    if (index >= ctx->suffix_count || index >= VIPER_SCPI_MAX_SUFFIXES ||
        ctx->suffix[index] < 0) {
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
    *str = ctx->buffer + ctx->params[idx].off;
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

/*
 * Decimal numeric program data (IEEE 488.2 7.7.2: NR1, NR2 or NR3, e.g. "42",
 * "1.5", "3.2E1"), times 10^@p shift, rounded to an integer, half away from
 * zero, as 488.2 and
 * SCPI-99 require for integer settings. Unsigned magnitude only; a value above
 * UINT32_MAX is OUT_OF_RANGE. 32-bit arithmetic without divisions, so it
 * pulls in no floating-point or 64-bit helpers.
 */
static viper_scpi_result_t parse_decimal(const char *s, size_t n, int32_t shift, uint32_t *mag)
{
    size_t i = 0;
    size_t mant_end;
    size_t ndigits = 0;
    size_t int_digits = 0;
    bool point = false;
    int32_t exp = 0;
    int32_t pos;   /* number of mantissa digits before the decimal point */
    int32_t k = 0; /* index of the current mantissa digit */
    uint32_t acc = 0;

    for (; i < n; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            ndigits++;
            if (!point) {
                int_digits++;
            }
        } else if (s[i] == '.' && !point) {
            point = true;
        } else {
            break;
        }
    }
    mant_end = i;
    if (ndigits == 0) {
        return VIPER_SCPI_RES_ERR_DATA_TYPE;
    }
    while (i < n && viper_scpi_is_space(s[i])) {
        i++; /* IEEE 488.2 7.7.2.2: white space before and after the E */
    }
    if (i < n) {
        bool eneg = false;
        size_t edigits = 0;

        if (viper_scpi_upper(s[i]) != 'E') {
            return VIPER_SCPI_RES_ERR_DATA_TYPE;
        }
        i++;
        while (i < n && viper_scpi_is_space(s[i])) {
            i++;
        }
        if (i < n && (s[i] == '+' || s[i] == '-')) {
            eneg = s[i] == '-';
            i++;
        }
        for (; i < n && s[i] >= '0' && s[i] <= '9'; i++, edigits++) {
            if (exp < 100000) {
                exp = exp * 10 + (s[i] - '0'); /* saturates: still decides */
            }
        }
        if (edigits == 0 || i != n) {
            return VIPER_SCPI_RES_ERR_DATA_TYPE;
        }
        if (eneg) {
            exp = -exp;
        }
    }

    pos = (int32_t)int_digits + exp + shift;
    for (i = 0; i < mant_end && k <= pos; i++) {
        uint32_t d;

        if (s[i] == '.') {
            continue;
        }
        d = (uint32_t)(s[i] - '0');
        if (k == pos) {
            /* First digit after the point: rounds the magnitude. */
            if (d >= 5u) {
                if (acc == UINT32_MAX) {
                    return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
                }
                acc++;
            }
        } else {
            /* acc * 10 + d > UINT32_MAX, without a division. */
            if (acc > 429496729u || (acc == 429496729u && d > 5u)) {
                return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
            }
            acc = acc * 10u + d;
        }
        k++;
    }
    for (; k < pos && acc != 0; k++) { /* zeros the exponent appends */
        if (acc > 429496729u) {
            return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
        }
        acc *= 10u;
    }
    *mag = acc;
    return VIPER_SCPI_RES_OK;
}

/* Parses sign and magnitude: a decimal number times 10^@p shift (rounded,
 * see parse_decimal()) or a #H/#Q/#B/0x non-decimal one, which is taken as
 * it is. A magnitude above UINT32_MAX is
 * OUT_OF_RANGE. 32-bit arithmetic only, so Cortex-M0 needs no 64-bit runtime
 * helpers. */
static viper_scpi_result_t parse_integer(const char *s, size_t n, int32_t shift, bool *neg,
                                         uint32_t *mag)
{
    uint32_t base = 10;
    unsigned bits = 4; /* log2(base) for the non-decimal bases */
    uint32_t acc = 0;
    uint32_t limit;
    size_t i = 0;

    *neg = false;
    if (n > 0 && s[0] == '#') {
        if (n < 3) {
            return VIPER_SCPI_RES_ERR_DATA_TYPE;
        }
        switch (viper_scpi_upper(s[1])) {
        case 'H': base = 16; bits = 4; break;
        case 'B': base = 2; bits = 1; break;
        case 'Q': base = 8; bits = 3; break;
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
        } else {
            return parse_decimal(s + i, n - i, shift, mag);
        }
    }
    if (i >= n) {
        return VIPER_SCPI_RES_ERR_DATA_TYPE;
    }
    /* acc * base + d overflows exactly when acc > UINT32_MAX / base: the
     * base is a power of two, so UINT32_MAX % base is base - 1, the largest
     * digit. Shifts, not divisions (no divide helper on Cortex-M0). */
    limit = UINT32_MAX >> bits;
    for (; i < n; i++) {
        int d = digit_value(s[i]);
        if (d < 0 || (uint32_t)d >= base) {
            return VIPER_SCPI_RES_ERR_DATA_TYPE;
        }
        if (acc > limit) {
            /* Keep validating: a later non-digit is a data type error. */
            for (i++; i < n; i++) {
                d = digit_value(s[i]);
                if (d < 0 || (uint32_t)d >= base) {
                    return VIPER_SCPI_RES_ERR_DATA_TYPE;
                }
            }
            return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
        }
        acc = (acc << bits) | (uint32_t)d;
    }
    *mag = acc;
    return VIPER_SCPI_RES_OK;
}

/* Sign and magnitude to int32_t; OUT_OF_RANGE beyond it. */
static viper_scpi_result_t to_int32(bool neg, uint32_t mag, int32_t *out)
{
    if (neg ? mag > (uint32_t)INT32_MAX + 1u : mag > (uint32_t)INT32_MAX) {
        return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
    }
    if (!neg) {
        *out = (int32_t)mag;
    } else if (mag == (uint32_t)INT32_MAX + 1u) {
        *out = INT32_MIN;
    } else {
        *out = -(int32_t)mag;
    }
    return VIPER_SCPI_RES_OK;
}

viper_scpi_result_t viper_scpi_param_int32(const viper_scpi_context_t *ctx, size_t idx,
                                           int32_t *out)
{
    const char *s;
    size_t n;
    bool neg;
    uint32_t mag;
    viper_scpi_result_t res;

    if (out == NULL) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    res = viper_scpi_param_raw(ctx, idx, &s, &n);
    if (res == VIPER_SCPI_RES_OK) {
        res = parse_integer(s, n, 0, &neg, &mag);
    }
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    return to_int32(neg, mag, out);
}

viper_scpi_result_t viper_scpi_param_uint32(const viper_scpi_context_t *ctx, size_t idx,
                                            uint32_t *out)
{
    const char *s;
    size_t n;
    bool neg;
    uint32_t mag;
    viper_scpi_result_t res;

    if (out == NULL) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    res = viper_scpi_param_raw(ctx, idx, &s, &n);
    if (res == VIPER_SCPI_RES_OK) {
        res = parse_integer(s, n, 0, &neg, &mag);
    }
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (neg && mag != 0) {
        return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
    }
    *out = (uint32_t)mag;
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
    if (n == 2 && viper_scpi_pattern_match("ON", s, n)) {
        *out = true;
    } else if (n == 3 && viper_scpi_pattern_match("OFF", s, n)) {
        *out = false;
    } else {
        /* SCPI-99 7.3: a number is rounded; non-zero is ON. */
        bool neg;
        uint32_t mag = 0;

        res = parse_integer(s, n, 0, &neg, &mag);
        if (res == VIPER_SCPI_RES_ERR_OUT_OF_RANGE) {
            mag = 1; /* huge, so not zero */
        } else if (res != VIPER_SCPI_RES_OK) {
            return VIPER_SCPI_RES_ERR_DATA_TYPE;
        }
        *out = mag != 0;
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
    /* Validate and measure first: a rejected parameter leaves @p out as it
     * was (it is often the instrument's current setting). */
    for (size_t i = 1; i < n - 1; i++) {
        if (s[i] == q) {
            if (i + 1 < n - 1 && s[i + 1] == q) {
                i++; /* doubled delimiter stands for one */
            } else {
                return VIPER_SCPI_RES_ERR_DATA_TYPE; /* stray delimiter */
            }
        }
        w++;
    }
    if (w >= out_size) {
        return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
    }
    w = 0;
    for (size_t i = 1; i < n - 1; i++) {
        out[w++] = s[i];
        if (s[i] == q) {
            i++;
        }
    }
    out[w] = '\0';
    if (out_len != NULL) {
        *out_len = w;
    }
    return VIPER_SCPI_RES_OK;
}

/* ---- MIN / MAX / DEF / UP / DOWN keywords ---------------------------------------------- */

viper_scpi_numeric_kw_t viper_scpi_numeric_keyword(const viper_scpi_context_t *ctx, size_t idx)
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

/* True for a unit whose scale is exactly 1.0. Checked on the bit pattern
 * (IEEE 754 binary64 1.0, or binary32 1.0 where double is 32-bit) so that
 * this translation unit does no floating-point arithmetic. */
static bool unit_scale_is_one(const viper_scpi_unit_t *u)
{
    union {
        double d;
        uint64_t u64;
        uint32_t u32;
    } bits;

    bits.u64 = 0;
    bits.d = u->scale;
    return (sizeof(double) == 8) ? bits.u64 == 0x3FF0000000000000u
                                 : bits.u32 == 0x3F800000u;
}

size_t viper_scpi_scan_number(const char *s, size_t n)
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
    /* IEEE 488.2 7.7.2.2: [white space] E [white space] [sign] digits. An
     * exponent needs digits; otherwise the 'E' starts a unit (EX = exa). */
    {
        size_t j = i;

        while (j < n && viper_scpi_is_space(s[j])) {
            j++;
        }
        if (j < n && viper_scpi_upper(s[j]) == 'E') {
            j++;
            while (j < n && viper_scpi_is_space(s[j])) {
                j++;
            }
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
    }
    return i;
}

/* A number of spec->units, as a count of 10^exp10 base units. */
static viper_scpi_result_t integer_value(const viper_scpi_integer_t *spec, const char *s,
                                         size_t n, int32_t *out)
{
    bool neg = false;
    uint32_t mag = 0;
    viper_scpi_result_t res;

    size_t sign = (n > 0 && (s[0] == '+' || s[0] == '-')) ? 1u : 0u;
    bool non_decimal = (n > 0 && s[0] == '#') ||
                       (n > sign + 2u && s[sign] == '0' && viper_scpi_upper(s[sign + 1]) == 'X');

    if (spec->units == NULL || non_decimal) {
        /* #H/#Q/#B and 0x numbers give the count directly, units or not
         * (they take no unit suffix). */
        res = parse_integer(s, n, -(int32_t)spec->exp10, &neg, &mag);
    } else {
        const viper_scpi_unit_t *unit;
        size_t num;
        size_t i = 0;
        int exp3;

        res = spec->units->split(spec->units->list, s, n, &num, &unit, &exp3);
        if (res != VIPER_SCPI_RES_OK) {
            return res;
        }
        if (unit != NULL && !unit_scale_is_one(unit)) {
            return VIPER_SCPI_RES_ERR_INVALID_SUFFIX; /* MIN, HOUR: not integral */
        }
        if (s[0] == '+' || s[0] == '-') {
            neg = s[0] == '-';
            i = 1;
        }
        res = parse_decimal(s + i, num - i, (int32_t)exp3 * 3 - (int32_t)spec->exp10, &mag);
    }
    return (res == VIPER_SCPI_RES_OK) ? to_int32(neg, mag, out) : res;
}

viper_scpi_result_t viper_scpi_param_integer(const viper_scpi_context_t *ctx, size_t idx,
                                             const viper_scpi_integer_t *spec, int32_t *out,
                                             viper_scpi_numeric_kw_t *kw_out)
{
    viper_scpi_numeric_kw_t kw;
    const char *s;
    size_t n;
    int32_t v;
    viper_scpi_result_t res;
    bool updown;

    if (spec == NULL || out == NULL || spec->min > spec->max ||
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
    res = viper_scpi_param_raw(ctx, idx, &s, &n);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    res = integer_value(spec, s, n, &v);
    if (res == VIPER_SCPI_RES_ERR_DATA_TYPE) {
        /* As in the double parsers: NAN is never in a range, and INF/NINF
         * lie outside any integer one. */
        if (viper_scpi_pattern_match("NAN", s, n)) {
            return VIPER_SCPI_RES_ERR_ILLEGAL_VALUE;
        }
        if (viper_scpi_pattern_match("INFinity", s, n) ||
            viper_scpi_pattern_match("NINFinity", s, n)) {
            return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
        }
    }
    if (res != VIPER_SCPI_RES_OK) {
        return res;
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

viper_scpi_result_t viper_scpi_param_register(const viper_scpi_context_t *ctx, int32_t max,
                                              int32_t *out)
{
    int32_t v;
    viper_scpi_result_t res = viper_scpi_param_int32(ctx, 0, &v);

    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (ctx->param_count > 1) {
        return VIPER_SCPI_RES_ERR_PARAM_NOT_ALLOWED;
    }
    if (v < 0 || v > max) {
        return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
    }
    *out = v;
    return VIPER_SCPI_RES_OK;
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
