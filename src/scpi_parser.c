/* Core SCPI state machine: line assembly, tokenizing, tree dispatch, output. */
#include <float.h>
#include <stdio.h>
#include <string.h>

#include "scpi_internal.h"

/* ---- Output ---------------------------------------------------------- */

static void emit(viper_scpi_context_t *ctx, const char *s, size_t n)
{
    if (ctx->cfg->write != NULL && n > 0) {
        (void)ctx->cfg->write(ctx->user_data, s, n);
    }
}

/* Writes the separator that precedes a response value. */
static void result_begin(viper_scpi_context_t *ctx)
{
    if (ctx->unit_has_output) {
        emit(ctx, ",", 1);
    } else if (ctx->line_has_output) {
        emit(ctx, ";", 1);
    }
    ctx->unit_has_output = 1;
    ctx->line_has_output = 1;
}

void viper_scpi_result_text(viper_scpi_context_t *ctx, const char *str)
{
    result_begin(ctx);
    emit(ctx, str, strlen(str));
}

void viper_scpi_result_int(viper_scpi_context_t *ctx, int32_t value)
{
    char buf[16];
    int n = snprintf(buf, sizeof buf, "%ld", (long)value);
    result_begin(ctx);
    emit(ctx, buf, (size_t)n);
}

void viper_scpi_result_uint(viper_scpi_context_t *ctx, uint32_t value)
{
    char buf[16];
    int n = snprintf(buf, sizeof buf, "%lu", (unsigned long)value);
    result_begin(ctx);
    emit(ctx, buf, (size_t)n);
}

void viper_scpi_result_bool(viper_scpi_context_t *ctx, bool value)
{
    result_begin(ctx);
    emit(ctx, value ? "1" : "0", 1);
}

void viper_scpi_result_double(viper_scpi_context_t *ctx, double value)
{
    char buf[32];
    const char *s = buf;

    /* SCPI defines 9.9E37 as infinity and 9.91E37 as "not a number". */
    if (value != value) {
        s = "9.91E+37";
    } else if (value > DBL_MAX) {
        s = "9.9E+37";
    } else if (value < -DBL_MAX) {
        s = "-9.9E+37";
    } else {
        (void)snprintf(buf, sizeof buf, "%.9g", value);
    }
    result_begin(ctx);
    emit(ctx, s, strlen(s));
}

void viper_scpi_result_string(viper_scpi_context_t *ctx, const char *str)
{
    const char *run = str;

    result_begin(ctx);
    emit(ctx, "\"", 1);
    for (const char *p = str; *p != '\0'; p++) {
        if (*p == '"') {
            emit(ctx, run, (size_t)(p - run) + 1); /* text and the quote */
            emit(ctx, "\"", 1);                    /* doubled quote */
            run = p + 1;
        }
    }
    emit(ctx, run, strlen(run));
    emit(ctx, "\"", 1);
}

/* ---- Small accessors --------------------------------------------------- */

void *viper_scpi_user_data(const viper_scpi_context_t *ctx)
{
    return ctx->user_data;
}

bool viper_scpi_is_query(const viper_scpi_context_t *ctx)
{
    return ctx->is_query != 0;
}

viper_scpi_result_t viper_scpi_no_params(const viper_scpi_context_t *ctx)
{
    return ctx->param_count > 0 ? VIPER_SCPI_RES_ERR_PARAM_NOT_ALLOWED
                                : VIPER_SCPI_RES_OK;
}

/* ---- Tokenizer ----------------------------------------------------------- */

typedef struct {
    const char *str;
    size_t len;
} segment_t;

/* Splits the parameter region of a unit into ctx->params. */
static viper_scpi_result_t split_params(viper_scpi_context_t *ctx, const char *p, size_t n)
{
    size_t start = 0;
    char quote = 0;

    while (n > 0 && viper_scpi_is_space(*p)) {
        p++;
        n--;
    }
    if (n == 0) {
        return VIPER_SCPI_RES_OK;
    }

    for (size_t i = 0; i <= n; i++) {
        if (i < n) {
            char c = p[i];
            if (quote != 0) {
                if (c == quote) {
                    quote = 0;
                }
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
                continue;
            }
            if (c != ',') {
                continue;
            }
        } else if (quote != 0) {
            return VIPER_SCPI_RES_ERR_SYNTAX; /* unterminated string */
        }

        size_t ts = start;
        size_t te = i;
        while (ts < te && viper_scpi_is_space(p[ts])) {
            ts++;
        }
        while (te > ts && viper_scpi_is_space(p[te - 1])) {
            te--;
        }
        if (ts == te) {
            return VIPER_SCPI_RES_ERR_SYNTAX; /* empty parameter */
        }
        if (ctx->param_count >= VIPER_SCPI_MAX_PARAMS) {
            return VIPER_SCPI_RES_ERR_PARAM_NOT_ALLOWED;
        }
        ctx->params[ctx->param_count].str = p + ts;
        ctx->params[ctx->param_count].len = te - ts;
        ctx->param_count++;
        start = i + 1;
    }
    return VIPER_SCPI_RES_OK;
}

/* ---- Tree traversal --------------------------------------------------------- */

/*
 * Walks @p segs through the tree starting at @p list. Intermediate segments
 * must name nodes with children, the last one a node with a callback.
 * @p parent_list receives the sibling list holding the last node.
 */
static const viper_scpi_node_t *walk(const viper_scpi_node_t *list,
                                     const segment_t *segs, size_t nseg,
                                     const viper_scpi_node_t **parent_list)
{
    const viper_scpi_node_t *found = NULL;

    for (size_t k = 0; k < nseg; k++) {
        bool last = (k == nseg - 1);

        found = NULL;
        for (const viper_scpi_node_t *n = list; n != NULL && n->pattern != NULL; n++) {
            if (!viper_scpi_pattern_match(n->pattern, segs[k].str, segs[k].len)) {
                continue;
            }
            if (last ? (n->callback != NULL) : (n->children != NULL)) {
                found = n;
                break;
            }
        }
        if (found == NULL) {
            return NULL;
        }
        *parent_list = list;
        list = found->children;
    }
    return found;
}

static viper_scpi_result_t dispatch_tree(viper_scpi_context_t *ctx, const char *h, size_t hlen)
{
    segment_t segs[VIPER_SCPI_MAX_RECURSION_DEPTH];
    size_t nseg = 0;
    size_t pos = 0;
    size_t st;
    const viper_scpi_node_t *parent = NULL;
    const viper_scpi_node_t *node;

    if (h[0] == ':') {
        ctx->level = NULL;
        pos = 1;
    }
    if (hlen > pos && h[hlen - 1] == '?') {
        ctx->is_query = 1;
        hlen--;
    }
    if (hlen <= pos) {
        return VIPER_SCPI_RES_ERR_SYNTAX;
    }

    st = pos;
    for (size_t i = pos; i <= hlen; i++) {
        if (i == hlen || h[i] == ':') {
            if (i == st) {
                return VIPER_SCPI_RES_ERR_SYNTAX; /* empty mnemonic */
            }
            if (nseg >= VIPER_SCPI_MAX_RECURSION_DEPTH) {
                return VIPER_SCPI_RES_ERR_MNEMONIC_TOO_LONG;
            }
            segs[nseg].str = h + st;
            segs[nseg].len = i - st;
            nseg++;
            st = i + 1;
        } else {
            char c = h[i];
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_';
            if (!ok) {
                return VIPER_SCPI_RES_ERR_SYNTAX;
            }
        }
    }

    if (ctx->level != NULL) {
        node = walk(ctx->level, segs, nseg, &parent);
    } else {
        node = walk(ctx->cfg->root, segs, nseg, &parent);
        if (node == NULL) {
            node = walk(viper_scpi_builtin_root, segs, nseg, &parent);
        }
    }
    if (node == NULL) {
        return VIPER_SCPI_RES_ERR_UNDEFINED_HEADER;
    }

    /* Later commands without a leading ':' are relative to the list that
     * holds this command (the children of its penultimate node). */
    if (nseg > 1) {
        ctx->level = parent;
    }
    return node->callback(ctx);
}

static viper_scpi_result_t dispatch(viper_scpi_context_t *ctx, const char *h, size_t hlen)
{
    if (h[0] == '*') {
        bool query = h[hlen - 1] == '?';
        size_t nlen = hlen - 1 - (query ? 1u : 0u);

        ctx->is_query = query ? 1 : 0;
        return viper_scpi_ieee488_dispatch(ctx, h + 1, nlen, query);
    }
    return dispatch_tree(ctx, h, hlen);
}

/* ---- Program message execution ----------------------------------------------- */

static void execute_unit(viper_scpi_context_t *ctx, const char *s, size_t n)
{
    size_t hlen = 0;
    viper_scpi_result_t res;

    ctx->unit_has_output = 0;
    ctx->param_count = 0;
    ctx->is_query = 0;

    while (n > 0 && viper_scpi_is_space(*s)) {
        s++;
        n--;
    }
    while (n > 0 && viper_scpi_is_space(s[n - 1])) {
        n--;
    }
    if (n == 0) {
        return; /* empty unit, e.g. "*RST;;*CLS" */
    }

    while (hlen < n && !viper_scpi_is_space(s[hlen])) {
        hlen++;
    }

    res = split_params(ctx, s + hlen, n - hlen);
    if (res == VIPER_SCPI_RES_OK) {
        res = dispatch(ctx, s, hlen);
    }
    if (res != VIPER_SCPI_RES_OK) {
        viper_scpi_error_push(ctx, (int)res);
    }
}

static void execute_line(viper_scpi_context_t *ctx)
{
    size_t start = 0;
    char quote = 0;

    ctx->level = NULL;
    ctx->line_has_output = 0;

    for (size_t i = 0; i <= ctx->length; i++) {
        if (i < ctx->length) {
            char c = ctx->buffer[i];
            if (quote != 0) {
                if (c == quote) {
                    quote = 0;
                }
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
                continue;
            }
            if (c != ';') {
                continue;
            }
        }
        execute_unit(ctx, ctx->buffer + start, i - start);
        start = i + 1;
    }

    if (ctx->line_has_output) {
        emit(ctx, "\n", 1);
    }
}

static void finish_line(viper_scpi_context_t *ctx)
{
    if (ctx->overflow) {
        viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_INPUT_OVERRUN);
    } else {
        execute_line(ctx);
    }
    ctx->length = 0;
    ctx->overflow = 0;
}

/* ---- Public input API --------------------------------------------------------- */

viper_scpi_result_t viper_scpi_init(viper_scpi_context_t *ctx, const viper_scpi_config_t *cfg)
{
    if (ctx == NULL || cfg == NULL) {
        return VIPER_SCPI_RES_ERR_PARAM_INVALID;
    }
    memset(ctx, 0, sizeof *ctx);
    ctx->cfg = cfg;
    ctx->user_data = cfg->user_data;
    return VIPER_SCPI_RES_OK;
}

void viper_scpi_input_char(viper_scpi_context_t *ctx, char c)
{
    if (c == '\n') {
        finish_line(ctx);
    } else if (c == '\r' || c == '\0' || ctx->overflow) {
        return;
    } else if (ctx->length >= VIPER_SCPI_BUFFER_SIZE) {
        ctx->overflow = 1;
    } else {
        ctx->buffer[ctx->length++] = c;
    }
}

void viper_scpi_input_buffer(viper_scpi_context_t *ctx, const char *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        viper_scpi_input_char(ctx, data[i]);
    }
}

void viper_scpi_end_of_message(viper_scpi_context_t *ctx)
{
    if (ctx->length > 0 || ctx->overflow) {
        finish_line(ctx);
    }
}
