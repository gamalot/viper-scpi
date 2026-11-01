/* Core SCPI state machine: line assembly, tokenizing, tree dispatch, output. */
#include <float.h>
#include <stdio.h>
#include <string.h>

#include "scpi_internal.h"

/* ---- Output ---------------------------------------------------------- */

static bool queued(const viper_scpi_context_t *ctx)
{
    return ctx->cfg->output_buffer != NULL;
}

static void output_reset(viper_scpi_context_t *ctx)
{
    ctx->out_len = 0;
    ctx->out_rd = 0;
    ctx->out_block_active = 0;
}

static void emit(viper_scpi_context_t *ctx, const char *s, size_t n)
{
    const viper_scpi_config_t *cfg = ctx->cfg;

    if (n == 0) {
        return;
    }
    if (!queued(ctx)) {
        if (cfg->write != NULL) {
            (void)cfg->write(ctx->user_data, s, n);
        }
        return;
    }
    if (ctx->out_overflow) {
        return;
    }
    if (n > cfg->output_size - ctx->out_len) {
        /* The whole response is dropped at the end of the line rather than
         * handed out truncated: a cut-off reply reads as a valid one. */
        ctx->out_overflow = 1;
        viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_OUT_OF_MEMORY);
        return;
    }
    memcpy(cfg->output_buffer + ctx->out_len, s, n);
    ctx->out_len += n;
}

bool viper_scpi_output_pending(const viper_scpi_context_t *ctx)
{
    if (ctx->wait != VIPER_SCPI_WAIT_NONE) {
        return false; /* the response message is not complete yet */
    }
    return ctx->out_rd < ctx->out_len ||
           (ctx->out_block_active && ctx->out_block_done < ctx->out_block_total);
}

size_t viper_scpi_output_read(viper_scpi_context_t *ctx, uint8_t *dst, size_t max, bool *end)
{
    const char *buf = ctx->cfg->output_buffer;
    size_t n = 0;

    if (ctx->wait != VIPER_SCPI_WAIT_NONE) {
        *end = false; /* nothing readable until the message finishes */
        return 0;
    }

    while (n < max) {
        size_t text_end = ctx->out_block_active ? ctx->out_block_at : ctx->out_len;
        size_t k;

        if (ctx->out_rd < text_end || (ctx->out_rd < ctx->out_len &&
                                       ctx->out_block_done >= ctx->out_block_total)) {
            /* text before the block, or after it once it is done */
            size_t stop = (ctx->out_rd < text_end) ? text_end : ctx->out_len;
            k = stop - ctx->out_rd;
            if (k > max - n) {
                k = max - n;
            }
            memcpy(dst + n, buf + ctx->out_rd, k);
            ctx->out_rd += k;
        } else if (ctx->out_block_active && ctx->out_block_done < ctx->out_block_total) {
            uint32_t left = ctx->out_block_total - ctx->out_block_done;
            k = (left < max - n) ? (size_t)left : max - n;
            ctx->out_fill(ctx->out_fill_user, dst + n, ctx->out_block_done, k);
            ctx->out_block_done += (uint32_t)k;
        } else {
            break;
        }
        n += k;
    }

    *end = !viper_scpi_output_pending(ctx);
    if (*end) {
        output_reset(ctx);
        viper_scpi_srq_update(ctx); /* MAV fell */
    }
    return n;
}

void viper_scpi_output_discard(viper_scpi_context_t *ctx)
{
    output_reset(ctx);
    viper_scpi_srq_update(ctx);
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
        (void)snprintf(buf, sizeof buf, "%.*g", digits, value);
        /* IEEE 488.2 8.7.4: the NR3 exponent marker is an upper-case E. */
        for (char *p = buf; *p != '\0'; p++) {
            if (*p == 'e') {
                *p = 'E';
            }
        }
    }
    result_begin(ctx);
    emit(ctx, s, strlen(s));
}

void viper_scpi_result_double(viper_scpi_context_t *ctx, double value)
{
    viper_scpi_result_double_digits(ctx, value, VIPER_SCPI_DOUBLE_DIGITS);
}

/* Writes the separator and "#<n><length>"; false if the length needs more
 * than the nine digits a definite-length header can carry. */
static bool block_header(viper_scpi_context_t *ctx, size_t len)
{
    char hdr[16];
    unsigned digits = 1;
    int n;

    for (size_t t = len; t >= 10; t /= 10) {
        digits++;
    }
    if (digits > 9) {
        viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_OUT_OF_RANGE);
        return false;
    }
    n = snprintf(hdr, sizeof hdr, "#%u%lu", digits, (unsigned long)len);
    result_begin(ctx);
    emit(ctx, hdr, (size_t)n);
    return true;
}

void viper_scpi_result_block(viper_scpi_context_t *ctx, const void *data, size_t len)
{
    if (block_header(ctx, len)) {
        emit(ctx, (const char *)data, len);
    }
}

void viper_scpi_result_block_fill(viper_scpi_context_t *ctx, uint32_t len,
                                  viper_scpi_block_fill_t fill, void *fill_user)
{
    if (fill == NULL) {
        viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_EXECUTION);
        return;
    }
    if (queued(ctx) && ctx->out_block_active) {
        /* One generated block per response: there is nowhere to put a second. */
        viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_QUERY);
        return;
    }
    if (!block_header(ctx, len)) {
        return;
    }

    if (queued(ctx)) {
        if (ctx->out_overflow) {
            return;
        }
        ctx->out_block_active = 1;
        ctx->out_block_at = ctx->out_len;
        ctx->out_block_total = len;
        ctx->out_block_done = 0;
        ctx->out_fill = fill;
        ctx->out_fill_user = fill_user;
        return;
    }

    /* Stream model: produce and write it now, a chunk at a time. */
    for (uint32_t off = 0; off < len;) {
        uint8_t chunk[64];
        size_t k = (len - off < sizeof chunk) ? (size_t)(len - off) : sizeof chunk;
        fill(fill_user, chunk, off, k);
        emit(ctx, (const char *)chunk, k);
        off += (uint32_t)k;
    }
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

typedef viper_scpi_token_t segment_t;

/* True if a block starts at p[i]: '#' at the start of a parameter. */
static size_t block_at(const char *p, size_t i, size_t n)
{
    if (p[i] != '#' || i == 0) {
        return 0;
    }
    if (p[i - 1] != ' ' && p[i - 1] != '\t' && p[i - 1] != ',') {
        return 0;
    }
    return viper_scpi_block_span(p + i, n - i);
}

/* Splits the parameter region of a unit into ctx->params. */
static viper_scpi_result_t split_params(viper_scpi_context_t *ctx, const char *p, size_t n)
{
    size_t i = 0;

    while (i < n && viper_scpi_is_space(p[i])) {
        i++;
    }
    if (i == n) {
        return VIPER_SCPI_RES_OK;
    }

    for (;;) {
        size_t ts;
        size_t te;
        size_t span;
        char quote = 0;

        while (i < n && viper_scpi_is_space(p[i])) {
            i++;
        }
        if (i >= n) {
            return VIPER_SCPI_RES_ERR_SYNTAX; /* empty parameter */
        }
        ts = i;
        span = block_at(p, i, n);
        if (span > 0) {
            i += span; /* raw block bytes are kept verbatim */
            te = i;
        } else {
            for (; i < n; i++) {
                if (quote != 0) {
                    if (p[i] == quote) {
                        quote = 0;
                    }
                } else if (p[i] == '"' || p[i] == '\'') {
                    quote = p[i];
                } else if (p[i] == ',') {
                    break;
                }
            }
            if (quote != 0) {
                return VIPER_SCPI_RES_ERR_SYNTAX; /* unterminated string */
            }
            te = i;
            while (te > ts && viper_scpi_is_space(p[te - 1])) {
                te--;
            }
            if (te == ts) {
                return VIPER_SCPI_RES_ERR_SYNTAX; /* empty parameter */
            }
        }
        if (ctx->param_count >= VIPER_SCPI_MAX_PARAMS) {
            return VIPER_SCPI_RES_ERR_PARAM_NOT_ALLOWED;
        }
        ctx->params[ctx->param_count].str = p + ts;
        ctx->params[ctx->param_count].len = te - ts;
        ctx->param_count++;

        while (i < n && viper_scpi_is_space(p[i])) {
            i++;
        }
        if (i >= n) {
            return VIPER_SCPI_RES_OK;
        }
        if (p[i] != ',') {
            return VIPER_SCPI_RES_ERR_SYNTAX; /* garbage after a block */
        }
        i++;
    }
}

/* ---- Tree traversal --------------------------------------------------------- */

/* Depth guard against malformed (cyclic) trees. */
#define MAX_TREE_DEPTH (2 * VIPER_SCPI_MAX_RECURSION_DEPTH)

static bool suffix_push(viper_scpi_context_t *ctx, int32_t value)
{
    if (ctx->suffix_count >= VIPER_SCPI_MAX_SUFFIXES) {
        return false;
    }
    ctx->suffix[ctx->suffix_count++] = value;
    return true;
}

/* Records -1 for every suffix mnemonic of a fully skipped pattern. */
static bool push_skipped_suffixes(viper_scpi_context_t *ctx, const char *pattern)
{
    viper_scpi_pattern_item_t it;

    while (viper_scpi_pattern_next(&pattern, &it) == 1) {
        if (it.has_suffix && !suffix_push(ctx, -1)) {
            return false;
        }
    }
    return true;
}

/* All segments are consumed: find an omittable node that has a callback. */
static const viper_scpi_node_t *find_default(viper_scpi_context_t *ctx,
                                             const viper_scpi_node_t *list, unsigned depth)
{
    uint8_t saved = ctx->suffix_count;

    if (list == NULL || depth > MAX_TREE_DEPTH) {
        return NULL;
    }
    for (const viper_scpi_node_t *n = list; n->pattern != NULL; n++) {
        const viper_scpi_node_t *hit;

        if (!viper_scpi_pattern_all_optional(n->pattern) ||
            !push_skipped_suffixes(ctx, n->pattern)) {
            ctx->suffix_count = saved;
            continue;
        }
        hit = (n->callback != NULL) ? n : find_default(ctx, n->children, depth + 1);
        if (hit != NULL) {
            return hit;
        }
        ctx->suffix_count = saved;
    }
    return NULL;
}

static const viper_scpi_node_t *resolve(viper_scpi_context_t *ctx,
                                        const viper_scpi_node_t *list,
                                        const segment_t *segs, size_t nseg, unsigned depth);

/*
 * Matches the remaining mnemonics of node @p n's pattern (starting at @p p)
 * against @p segs, trying to consume optional mnemonics first and to skip
 * them second. Once the pattern is used up, the remaining segments continue
 * in the node's children.
 */
static const viper_scpi_node_t *match_items(viper_scpi_context_t *ctx,
                                            const viper_scpi_node_t *n, const char *p,
                                            const segment_t *segs, size_t nseg, unsigned depth)
{
    viper_scpi_pattern_item_t it;
    uint8_t saved = ctx->suffix_count;
    const viper_scpi_node_t *hit;
    int32_t sfx;

    switch (viper_scpi_pattern_next(&p, &it)) {
    case 0:
        if (nseg == 0) {
            return (n->callback != NULL) ? n : find_default(ctx, n->children, depth + 1);
        }
        return resolve(ctx, n->children, segs, nseg, depth + 1);
    case 1:
        break;
    default:
        return NULL; /* malformed pattern */
    }

    if (nseg > 0 && viper_scpi_item_match(&it, segs[0].str, segs[0].len, &sfx) &&
        (!it.has_suffix || suffix_push(ctx, sfx))) {
        hit = match_items(ctx, n, p, segs + 1, nseg - 1, depth);
        if (hit != NULL) {
            return hit;
        }
        ctx->suffix_count = saved;
    }
    if (it.optional && (!it.has_suffix || suffix_push(ctx, -1))) {
        hit = match_items(ctx, n, p, segs, nseg, depth);
        if (hit != NULL) {
            return hit;
        }
        ctx->suffix_count = saved;
    }
    return NULL;
}

/*
 * Matches @p segs against the tree below @p list, backtracking over optional
 * mnemonics. Suffixes are recorded in ctx->suffix.
 */
static const viper_scpi_node_t *resolve(viper_scpi_context_t *ctx,
                                        const viper_scpi_node_t *list,
                                        const segment_t *segs, size_t nseg, unsigned depth)
{
    if (list == NULL || depth > MAX_TREE_DEPTH) {
        return NULL;
    }
    for (const viper_scpi_node_t *n = list; n->pattern != NULL; n++) {
        const viper_scpi_node_t *hit =
            match_items(ctx, n, n->pattern, segs, nseg, depth);
        if (hit != NULL) {
            return hit;
        }
    }
    return NULL;
}

static viper_scpi_result_t dispatch_tree(viper_scpi_context_t *ctx, const char *h, size_t hlen)
{
    segment_t segs[VIPER_SCPI_MAX_RECURSION_DEPTH];
    size_t nseg = 0;
    size_t pos = 0;
    size_t st;
    const viper_scpi_node_t *node;

    ctx->suffix_count = 0;

    /* A leading ':' is absolute; otherwise the header is appended to the path
     * of the previous command without its last mnemonic (SCPI 6.2.1). */
    if (h[0] == ':') {
        ctx->prefix_count = 0;
        pos = 1;
    }
    for (; nseg < ctx->prefix_count; nseg++) {
        segs[nseg] = ctx->prefix[nseg];
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

    node = resolve(ctx, ctx->cfg->root, segs, nseg, 0);
    if (node == NULL) {
        ctx->suffix_count = 0;
        node = resolve(ctx, viper_scpi_builtin_root, segs, nseg, 0);
    }
    if (node == NULL) {
        return VIPER_SCPI_RES_ERR_UNDEFINED_HEADER;
    }

    /* Remember everything but the last mnemonic for relative headers. */
    for (size_t i = 0; i + 1 < nseg; i++) {
        ctx->prefix[i] = segs[i];
    }
    ctx->prefix_count = (uint8_t)(nseg - 1);
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
    viper_scpi_srq_update(ctx);
}

enum { BLK_NONE, BLK_HASH, BLK_LENGTH, BLK_DATA, BLK_INDEFINITE };

static void input_reset(viper_scpi_context_t *ctx)
{
    ctx->length = 0;
    ctx->overflow = 0;
    ctx->block_state = BLK_NONE;
    ctx->quote = 0;
    ctx->last = 0;
}

/*
 * Executes the units of the buffered line from ctx->exec_pos. Returns early,
 * keeping the line, when a unit suspends execution (*WAI, or *OPC? with
 * operations pending); viper_scpi_poll() calls it again to continue.
 */
static void run_line(viper_scpi_context_t *ctx)
{
    size_t start = ctx->exec_pos;
    char quote = 0;

    for (size_t i = start; i <= ctx->length; i++) {
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
            if (c == '#') {
                size_t span = block_at(ctx->buffer, i, ctx->length);
                if (span > 0) {
                    i += span - 1; /* skip raw block bytes */
                    continue;
                }
            }
            if (c != ';') {
                continue;
            }
        }
        execute_unit(ctx, ctx->buffer + start, i - start);
        start = i + 1;
        if (ctx->wait != VIPER_SCPI_WAIT_NONE) {
            ctx->exec_pos = start;
            return;
        }
    }

    if (ctx->line_has_output) {
        emit(ctx, "\n", 1);
    }
    /* The response message is complete; separators start afresh. */
    ctx->line_has_output = 0;
    ctx->unit_has_output = 0;
    if (ctx->out_overflow) {
        output_reset(ctx); /* -321 already queued; no partial reply */
    }
    input_reset(ctx);
    viper_scpi_srq_update(ctx); /* MAV rose */
}

static void execute_line(viper_scpi_context_t *ctx)
{
    ctx->prefix_count = 0;
    ctx->line_has_output = 0;
    ctx->out_overflow = 0;
    ctx->exec_pos = 0;
    run_line(ctx);
}

static void finish_line(viper_scpi_context_t *ctx)
{
    if (ctx->wait != VIPER_SCPI_WAIT_NONE) {
        /* Input was refused while suspended (viper_scpi_busy()). */
        if (ctx->input_dropped) {
            ctx->input_dropped = 0;
            viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_INPUT_OVERRUN);
        }
        return;
    }
    if (ctx->length == 0 && !ctx->overflow) {
        input_reset(ctx);
        return; /* an empty line is not a program message */
    }
    if (queued(ctx) && viper_scpi_output_pending(ctx)) {
        /* IEEE 488.2 6.3.2.3: a new message over an unread reply ends it. */
        output_reset(ctx);
        viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_QUERY_INTERRUPTED);
    }
    if (ctx->block_state == BLK_LENGTH || ctx->block_state == BLK_DATA) {
        /* The message ended inside a definite-length block: the controller
         * did not send what it declared, so the command must not run. */
        viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_INVALID_BLOCK);
    } else if (ctx->overflow) {
        viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_INPUT_OVERRUN);
    } else {
        execute_line(ctx); /* resets the input unless it suspends */
        return;
    }
    input_reset(ctx);
}

static void store(viper_scpi_context_t *ctx, char c)
{
    if (ctx->overflow) {
        return;
    }
    if (ctx->length >= VIPER_SCPI_BUFFER_SIZE) {
        ctx->overflow = 1;
    } else {
        ctx->buffer[ctx->length++] = c;
    }
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
    ctx->esr = VIPER_SCPI_ESR_PON; /* IEEE 488.2 11.5.1.1.2: power on */
    viper_scpi_status_preset(ctx);
    return VIPER_SCPI_RES_OK;
}

void viper_scpi_input_discard(viper_scpi_context_t *ctx)
{
    if (ctx->wait != VIPER_SCPI_WAIT_NONE) {
        /* The buffer holds the rest of a message being executed, not
         * partial input; only forget bytes refused meanwhile. */
        ctx->input_dropped = 0;
        return;
    }
    input_reset(ctx);
}

void viper_scpi_device_clear(viper_scpi_context_t *ctx)
{
    /* IEEE 488.2: device clear returns to the Operation Complete Command
     * and Query Idle States, so a suspended message and an armed *OPC are
     * cancelled. Operations themselves belong to the application. */
    ctx->wait = VIPER_SCPI_WAIT_NONE;
    ctx->opc_armed = 0;
    ctx->input_dropped = 0;
    viper_scpi_input_discard(ctx);
    ctx->prefix_count = 0;
    viper_scpi_output_discard(ctx);
}

/*
 * Block data may contain any byte, including '\n' and ';', so the input side
 * has to recognise blocks while the line is still being assembled. The rules
 * mirror block_at() used when the line is executed.
 */
void viper_scpi_input_char(viper_scpi_context_t *ctx, char c)
{
    if (ctx->wait != VIPER_SCPI_WAIT_NONE) {
        if (c == '\n') {
            finish_line(ctx);
        } else {
            ctx->input_dropped = 1;
        }
        return;
    }
    switch (ctx->block_state) {
    case BLK_DATA:
        store(ctx, c);
        if (--ctx->block_remaining == 0) {
            ctx->block_state = BLK_NONE;
        }
        return;
    case BLK_INDEFINITE:
        if (c == '\n') {
            finish_line(ctx);
        } else {
            store(ctx, c);
        }
        return;
    case BLK_HASH:
        if (c == '0') {
            ctx->block_state = BLK_INDEFINITE;
            store(ctx, c);
            return;
        }
        if (c >= '1' && c <= '9') {
            ctx->block_state = BLK_LENGTH;
            ctx->block_digits = (uint8_t)(c - '0');
            ctx->block_remaining = 0;
            store(ctx, c);
            return;
        }
        ctx->block_state = BLK_NONE; /* "#H..." etc.: ordinary character */
        break;
    case BLK_LENGTH:
        if (c >= '0' && c <= '9') {
            ctx->block_remaining = ctx->block_remaining * 10u + (uint32_t)(c - '0');
            store(ctx, c);
            if (--ctx->block_digits == 0) {
                ctx->block_state = (ctx->block_remaining > 0) ? BLK_DATA : BLK_NONE;
            }
            return;
        }
        ctx->block_state = BLK_NONE; /* malformed length: parsed as text */
        break;
    default:
        break;
    }

    /* In the message-exchange model the message end is signalled out of band
     * (EOM / END), so a newline or carriage return inside an open quote is
     * string data (IEEE 488.2 7.7.5: any 7-bit ASCII character may appear
     * between the delimiters). On stream transports LF stays the only
     * terminator, even inside quotes, so one lost quote cannot swallow every
     * following message. */
    if (ctx->quote != 0 && queued(ctx) && (c == '\n' || c == '\r')) {
        store(ctx, c);
        ctx->last = c;
        return;
    }
    if (c == '\n') {
        finish_line(ctx);
        return;
    }
    if (c == '\r' || c == '\0') {
        return;
    }
    if (ctx->quote != 0) {
        if (c == ctx->quote) {
            ctx->quote = 0;
        }
    } else if (c == '"' || c == '\'') {
        ctx->quote = c;
    } else if (c == '#' && (ctx->last == ' ' || ctx->last == '\t' || ctx->last == ',')) {
        ctx->block_state = BLK_HASH;
    }
    store(ctx, c);
    ctx->last = c;
}

void viper_scpi_input_buffer(viper_scpi_context_t *ctx, const char *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        viper_scpi_input_char(ctx, data[i]);
    }
}

void viper_scpi_end_of_message(viper_scpi_context_t *ctx)
{
    finish_line(ctx);
}

/* ---- Overlapped commands ------------------------------------------------------ */

void viper_scpi_op_begin(viper_scpi_context_t *ctx)
{
    if (ctx->op_count < UINT8_MAX) {
        ctx->op_count++;
    }
}

void viper_scpi_op_end(viper_scpi_context_t *ctx)
{
    if (ctx->op_count == 0) {
        return;
    }
    if (--ctx->op_count == 0 && ctx->opc_armed) {
        ctx->opc_armed = 0;
        viper_scpi_event_set(ctx, VIPER_SCPI_ESR_OPC); /* may raise SRQ */
    }
}

bool viper_scpi_op_pending(const viper_scpi_context_t *ctx)
{
    return ctx->op_count != 0;
}

bool viper_scpi_busy(const viper_scpi_context_t *ctx)
{
    return ctx->wait != VIPER_SCPI_WAIT_NONE;
}

void viper_scpi_poll(viper_scpi_context_t *ctx)
{
    if (ctx->wait == VIPER_SCPI_WAIT_NONE || ctx->op_count != 0) {
        return;
    }
    if (ctx->wait == VIPER_SCPI_WAIT_OPCQ) {
        /* Finish the suspended *OPC? unit: its "1" follows any earlier
         * response of the message, separated by ';'. */
        ctx->wait = VIPER_SCPI_WAIT_NONE;
        ctx->unit_has_output = 0;
        viper_scpi_result_int(ctx, 1);
    }
    ctx->wait = VIPER_SCPI_WAIT_NONE;
    run_line(ctx);
}
