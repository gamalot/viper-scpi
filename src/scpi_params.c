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
