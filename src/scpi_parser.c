/* Core SCPI state machine: line assembly, tokenizing, tree dispatch, output. */
#include <string.h>

#include "scpi_internal.h"

/* ---- Output ---------------------------------------------------------- */

static bool queued(const viper_scpi_context_t *ctx)
{
    return ctx->cfg->output_buffer != NULL;
}

/* VIPER_SCPI_OPT_EOM_MESSAGES in effect: LF is a command separator. */
static bool lf_separates(const viper_scpi_context_t *ctx)
{
    return (ctx->cfg->options & VIPER_SCPI_OPT_EOM_MESSAGES) != 0 && queued(ctx);
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

/* The output queue holds response bytes, complete or not. */
bool viper_scpi_output_queued(const viper_scpi_context_t *ctx)
{
    return ctx->out_rd < ctx->out_len ||
           (ctx->out_block_active && ctx->out_block_done < ctx->out_block_total);
}

bool viper_scpi_output_pending(const viper_scpi_context_t *ctx)
{
    if (ctx->wait != VIPER_SCPI_WAIT_NONE || ctx->msg_started) {
        return false; /* the response message is not complete yet */
    }
    return viper_scpi_output_queued(ctx);
}

size_t viper_scpi_output_read(viper_scpi_context_t *ctx, uint8_t *dst, size_t max, bool *end)
{
    const char *buf = ctx->cfg->output_buffer;
    size_t n = 0;

    if (ctx->wait != VIPER_SCPI_WAIT_NONE || ctx->msg_started) {
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
    /* Whatever a still-executing (suspended) message writes next starts a
     * new response message, without a leading separator. */
    ctx->line_has_output = 0;
    ctx->unit_has_output = 0;
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

/* Decimal digits of @p v, written backwards from @p end; returns the start.
 * Done by hand so that integer responses do not pull in printf. */
static char *format_u32(char *end, uint32_t v)
{
    do {
        *--end = (char)('0' + v % 10u);
        v /= 10u;
    } while (v != 0);
    return end;
}

void viper_scpi_result_int(viper_scpi_context_t *ctx, int32_t value)
{
    char buf[12];
    char *end = buf + sizeof buf;
    /* Magnitude in unsigned arithmetic, so INT32_MIN is handled too. */
    char *p = format_u32(end, value < 0 ? 0u - (uint32_t)value : (uint32_t)value);

    if (value < 0) {
        *--p = '-';
    }
    result_begin(ctx);
    emit(ctx, p, (size_t)(end - p));
}

void viper_scpi_result_uint(viper_scpi_context_t *ctx, uint32_t value)
{
    char buf[12];
    char *end = buf + sizeof buf;
    char *p = format_u32(end, value);

    result_begin(ctx);
    emit(ctx, p, (size_t)(end - p));
}

void viper_scpi_result_bool(viper_scpi_context_t *ctx, bool value)
{
    result_begin(ctx);
    emit(ctx, value ? "1" : "0", 1);
}

/* Writes the separator and "#<n><length>"; false if the length needs more
 * than the nine digits a definite-length header can carry. */
static bool block_header(viper_scpi_context_t *ctx, size_t len)
{
    char hdr[12];
    char *end = hdr + sizeof hdr;
    char *p;
    char digits;

    if (len > 999999999u) {
        viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_OUT_OF_RANGE);
        return false;
    }
    p = format_u32(end, (uint32_t)len);
    digits = (char)('0' + (end - p)); /* 1..9 */
    p -= 2;
    p[0] = '#';
    p[1] = digits;
    result_begin(ctx);
    emit(ctx, p, (size_t)(end - p));
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

/* String body with embedded double quotes doubled. */
static void emit_escaped(viper_scpi_context_t *ctx, const char *str)
{
    const char *run = str;

    for (const char *p = str; *p != '\0'; p++) {
        if (*p == '"') {
            emit(ctx, run, (size_t)(p - run) + 1); /* text and the quote */
            emit(ctx, "\"", 1);                    /* doubled quote */
            run = p + 1;
        }
    }
    emit(ctx, run, strlen(run));
}

void viper_scpi_result_string_pair(viper_scpi_context_t *ctx, const char *a, const char *b)
{
    result_begin(ctx);
    emit(ctx, "\"", 1);
    emit_escaped(ctx, a);
    if (b != NULL) {
        emit(ctx, ";", 1);
        emit_escaped(ctx, b);
    }
    emit(ctx, "\"", 1);
}

void viper_scpi_result_string(viper_scpi_context_t *ctx, const char *str)
{
    viper_scpi_result_string_pair(ctx, str, NULL);
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

/* True if a parameter, and so possibly a block, can start after @p prev.
 * The input state machine applies the same rule to ctx->last, with 0 for the
 * start of a message. */
static bool param_boundary(char prev)
{
    return viper_scpi_is_space(prev) || prev == ',';
}

/* Length of the block starting at p[i] ('#' at the start of a parameter),
 * or 0. */
static size_t block_at(const char *p, size_t i, size_t n)
{
    if (p[i] != '#' || (i > 0 && !param_boundary(p[i - 1]))) {
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
        unsigned depth = 0;

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
            /* A ',' inside quotes or inside parentheses (expression data
             * such as the channel list "(@1,2,3)", IEEE 488.2 7.7.7) does
             * not separate parameters. */
            for (; i < n; i++) {
                char c = p[i];
                if (quote != 0) {
                    if (c == quote) {
                        quote = 0;
                    }
                } else if (c == '"' || c == '\'') {
                    quote = c;
                } else if (c == '(') {
                    depth++;
                } else if (c == ')' && depth > 0) {
                    depth--;
                } else if (c == ',' && depth == 0) {
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
            if (p[ts] == '#' && (te == ts + 1 || viper_scpi_is_space(p[ts + 1]) ||
                                 (p[ts + 1] >= '1' && p[ts + 1] <= '9'))) {
                /* '#' begins only non-decimal numbers (#H/#Q/#B, IEEE 488.2
                 * 7.7.4) and blocks (#<digit>, 7.7.6). A '#' followed by a
                 * separator or whitespace begins neither, and a definite
                 * block that is not complete here ("#1", "#25x") was cut
                 * short. Both are reported as a block cut short, so the code
                 * does not depend on where the message happened to end. */
                return VIPER_SCPI_RES_ERR_INVALID_BLOCK;
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

/* Records the suffix of the next '#' mnemonic of the path. Only the first
 * VIPER_SCPI_MAX_SUFFIXES are kept; further ones still count, so the
 * indexes of viper_scpi_suffix() stay right and a deeper path still
 * matches. */
static void suffix_push(viper_scpi_context_t *ctx, int32_t value)
{
    if (ctx->suffix_count < VIPER_SCPI_MAX_SUFFIXES) {
        ctx->suffix[ctx->suffix_count] = value;
    }
    if (ctx->suffix_count < UINT8_MAX) {
        ctx->suffix_count++;
    }
}

/* Records -1 for every suffix mnemonic of a fully skipped pattern. */
static void push_skipped_suffixes(viper_scpi_context_t *ctx, const char *pattern)
{
    viper_scpi_pattern_item_t it;

    while (viper_scpi_pattern_next(&pattern, &it) == 1) {
        if (it.has_suffix) {
            suffix_push(ctx, -1);
        }
    }
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

        if (!viper_scpi_pattern_all_optional(n->pattern)) {
            continue;
        }
        push_skipped_suffixes(ctx, n->pattern);
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

    if (nseg > 0 && viper_scpi_item_match(&it, segs[0].str, segs[0].len, &sfx)) {
        if (it.has_suffix) {
            suffix_push(ctx, sfx);
        }
        hit = match_items(ctx, n, p, segs + 1, nseg - 1, depth);
        if (hit != NULL) {
            return hit;
        }
        ctx->suffix_count = saved;
    }
    if (it.optional) {
        if (it.has_suffix) {
            suffix_push(ctx, -1);
        }
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

/* Resolves a tree header to its node, recording the path for relative
 * headers, without running the callback. */
static viper_scpi_result_t resolve_header(viper_scpi_context_t *ctx, const char *h, size_t hlen,
                                          const viper_scpi_node_t **out)
{
    segment_t segs[VIPER_SCPI_MAX_RECURSION_DEPTH];
    size_t nseg = 0;
    size_t pos = 0;
    size_t st;
    size_t npre;
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
    npre = nseg;
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
    if (node == NULL && npre > 0 &&
        (ctx->cfg->options & VIPER_SCPI_OPT_RELATIVE_FALLBACK) != 0) {
        /* Lenient: retry the header as if it had a leading ':'. */
        nseg -= npre;
        memmove(segs, segs + npre, nseg * sizeof segs[0]);
        ctx->suffix_count = 0;
        node = resolve(ctx, ctx->cfg->root, segs, nseg, 0);
        if (node == NULL) {
            ctx->suffix_count = 0;
            node = resolve(ctx, viper_scpi_builtin_root, segs, nseg, 0);
        }
    }
    if (node == NULL) {
        return VIPER_SCPI_RES_ERR_UNDEFINED_HEADER;
    }

    /* Remember everything but the last mnemonic for relative headers. */
    for (size_t i = 0; i + 1 < nseg; i++) {
        ctx->prefix[i] = segs[i];
    }
    ctx->prefix_count = (uint8_t)(nseg - 1);
    *out = node;
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t dispatch_tree(viper_scpi_context_t *ctx, const char *h, size_t hlen)
{
    const viper_scpi_node_t *node = NULL;
    viper_scpi_result_t res = resolve_header(ctx, h, hlen, &node);

    return (res == VIPER_SCPI_RES_OK) ? node->callback(ctx) : res;
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

enum {
    BLK_NONE, BLK_HASH, BLK_LENGTH, BLK_DATA, BLK_INDEFINITE,
    BLK_STREAM,       /* block bytes go to ctx->stream */
    BLK_SKIP,         /* block bytes are dropped (handler failed) */
    BLK_TAIL          /* after a streamed block, until the unit ends */
};

/*
 * Framing state of the message being received: where it ends, and whether
 * its bytes are kept. It is separate from the line buffer because a message
 * may arrive (and be refused) while the previous one is still executing.
 */
static void framing_reset(viper_scpi_context_t *ctx)
{
    ctx->overflow = 0;
    ctx->skip_unit = 0;
    ctx->block_state = BLK_NONE;
    ctx->quote = 0;
    ctx->last = 0;
}

static void input_reset(viper_scpi_context_t *ctx)
{
    ctx->length = 0;
    ctx->unit_start = 0;
    framing_reset(ctx);
}

static bool streaming(const viper_scpi_context_t *ctx)
{
    return ctx->block_state >= BLK_STREAM;
}

/* The message ended inside a block: right after its '#', in its length
 * digits or in its data. */
static bool block_cut(const viper_scpi_context_t *ctx)
{
    return ctx->block_state == BLK_HASH || ctx->block_state == BLK_LENGTH ||
           ctx->block_state == BLK_DATA;
}

/* A LF that ends the program message (unless it is data or a separator). */
static bool is_terminator(const viper_scpi_context_t *ctx, char c)
{
    return c == '\n' && !lf_separates(ctx);
}

/* Tells the handler a streamed block was cut short. */
static void stream_abort(viper_scpi_context_t *ctx)
{
    if (ctx->block_state == BLK_STREAM || ctx->block_state == BLK_TAIL) {
        (void)ctx->stream(ctx, VIPER_SCPI_STREAM_ABORT, NULL, 0);
    }
}

/* Calls the handler; a failure is queued and stops further calls. */
static bool stream_call(viper_scpi_context_t *ctx, viper_scpi_stream_event_t ev,
                        const uint8_t *data, size_t len)
{
    viper_scpi_result_t res = ctx->stream(ctx, ev, data, len);

    if (res != VIPER_SCPI_RES_OK) {
        viper_scpi_error_push(ctx, (int)res);
        return false;
    }
    return true;
}

/*
 * The streamed unit ended (';' or message end): END, unless the block or the
 * unit was cut short. The units that follow are buffered after the streamed
 * command's header, which stays in the buffer: its mnemonics are the path
 * for relative headers (SCPI-99 6.2.4), and ctx->exec_pos marks its end.
 */
static void stream_unit_done(viper_scpi_context_t *ctx)
{
    if (ctx->block_state == BLK_TAIL) {
        ctx->unit_has_output = 0;
        (void)stream_call(ctx, VIPER_SCPI_STREAM_END, NULL, 0);
    }
    framing_reset(ctx);
    ctx->last = ';';               /* what ended the unit */
    ctx->length = ctx->exec_pos;   /* keep the header */
    ctx->unit_start = ctx->length; /* later units do not stream */
    viper_scpi_srq_update(ctx);
}

/* A program message starts executing: response separators, path and
 * overflow state start afresh. */
static void message_begin(viper_scpi_context_t *ctx)
{
    ctx->prefix_count = 0;
    ctx->line_has_output = 0;
    ctx->out_overflow = 0;
}

/*
 * The program message is complete, or abandoned after part of it ran (a
 * streamed first command): terminate its response and free the line buffer.
 * This is the only place a response message ends. The framing state is left
 * alone: it may already describe input refused while the message was
 * suspended, which is still being discarded.
 */
static void message_end(viper_scpi_context_t *ctx)
{
    if (ctx->line_has_output) {
        emit(ctx, "\n", 1);
    }
    ctx->line_has_output = 0;
    ctx->unit_has_output = 0;
    if (ctx->out_overflow) {
        output_reset(ctx); /* -321 already queued; no partial reply */
    }
    ctx->msg_started = 0;
    ctx->length = 0;
    ctx->unit_start = 0;
    viper_scpi_srq_update(ctx); /* MAV rose */
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

    ctx->running = 1;
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
            if (c != ';' && c != '\n') {
                continue; /* '\n' is stored only as a separator (EOM_MESSAGES) */
            }
        }
        execute_unit(ctx, ctx->buffer + start, i - start);
        if (i < ctx->length && ctx->buffer[i] == '\n') {
            ctx->prefix_count = 0; /* a new line starts at the root */
        }
        start = i + 1;
        if (ctx->wait != VIPER_SCPI_WAIT_NONE) {
            ctx->exec_pos = start;
            ctx->running = 0;
            return;
        }
    }
    ctx->running = 0;
    message_end(ctx);
}

/*
 * The received line is complete: execute it. ctx->msg_started stays set
 * until message_end(), so the response stays hidden (no MAV, nothing to
 * read) while the units run, are suspended, or call back into the library.
 */
static void execute_line(viper_scpi_context_t *ctx)
{
    if (!ctx->msg_started) {
        message_begin(ctx);
        ctx->msg_started = 1;
        ctx->exec_pos = 0;
    } /* else a streamed first unit ran; exec_pos is the end of its header */
    framing_reset(ctx); /* from here on it describes the next message */
    run_line(ctx);
}

/* Only white space (and LF separators): not a program message. */
static bool blank_line(const viper_scpi_context_t *ctx)
{
    for (size_t i = 0; i < ctx->length; i++) {
        if (!viper_scpi_is_space(ctx->buffer[i]) && ctx->buffer[i] != '\n') {
            return false;
        }
    }
    return true;
}

static void finish_line(viper_scpi_context_t *ctx)
{
    if (ctx->wait != VIPER_SCPI_WAIT_NONE) {
        /* The end of a message refused while another one is suspended
         * (viper_scpi_busy()): it was dropped as a whole. */
        if (ctx->overflow) {
            viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_INPUT_OVERRUN);
        }
        framing_reset(ctx);
        return;
    }
    if (streaming(ctx) || ctx->skip_unit) {
        if (ctx->block_state == BLK_STREAM || ctx->block_state == BLK_SKIP ||
            (ctx->skip_unit && block_cut(ctx))) {
            stream_abort(ctx); /* EOM before the declared length */
            viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_INVALID_BLOCK);
        }
        stream_unit_done(ctx);
        /* The message ends here; finish its response below. */
    }
    if (!ctx->msg_started && !ctx->overflow && blank_line(ctx)) {
        input_reset(ctx);
        return; /* an empty line is not a program message */
    }
    if (!ctx->msg_started && queued(ctx) && viper_scpi_output_pending(ctx)) {
        /* IEEE 488.2 6.3.2.3: a new message over an unread reply ends it. */
        output_reset(ctx);
        viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_QUERY_INTERRUPTED);
    }
    if (block_cut(ctx)) {
        /* The controller did not send the block it started, so no command
         * of the message runs. */
        viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_INVALID_BLOCK);
    } else if (ctx->overflow) {
        viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_INPUT_OVERRUN);
    } else {
        execute_line(ctx); /* resets the input unless it suspends */
        return;
    }
    if (ctx->msg_started) {
        message_end(ctx); /* a streamed first command already ran */
    }
    input_reset(ctx);
}

/* Keeps a byte of the message being assembled, unless it is being dropped
 * (overrun, refused while suspended) or skipped (rest of a failed unit). */
static void store(viper_scpi_context_t *ctx, char c)
{
    if (ctx->overflow || ctx->skip_unit || ctx->wait != VIPER_SCPI_WAIT_NONE) {
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
         * partial input; only forget a message refused meanwhile. */
        framing_reset(ctx);
        return;
    }
    stream_abort(ctx);
    if (ctx->msg_started) {
        message_end(ctx); /* a streamed first command already ran */
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
    ctx->line_has_output = 0; /* the cleared reply gets no terminator */
    ctx->unit_has_output = 0;
    viper_scpi_input_discard(ctx);
    ctx->prefix_count = 0;
    viper_scpi_output_discard(ctx);
}

static const viper_scpi_stream_node_t *find_stream_node(const viper_scpi_context_t *ctx,
                                                       const viper_scpi_node_t *node)
{
    for (const viper_scpi_stream_node_t *s = ctx->cfg->stream_nodes; s != NULL && s->node != NULL;
         s++) {
        if (s->node == node && s->handler != NULL) {
            return s;
        }
    }
    return NULL;
}

/*
 * The length of a definite block has just been read. If the block belongs to
 * the first command of the message and that command's node opted in to
 * streaming, resolve the command now -- before its data arrives -- and hand
 * the data to the handler instead of the line buffer.
 */
static bool stream_start(viper_scpi_context_t *ctx)
{
    const viper_scpi_stream_node_t *sn;
    const viper_scpi_node_t *node = NULL;
    const char *s = ctx->buffer;
    size_t n = ctx->hash_pos;
    size_t hlen = 0;

    if (ctx->cfg->stream_nodes == NULL || ctx->unit_start != 0 || ctx->overflow ||
        ctx->msg_started || ctx->wait != VIPER_SCPI_WAIT_NONE) {
        return false;
    }
    while (n > 0 && viper_scpi_is_space(*s)) {
        s++;
        n--;
    }
    while (hlen < n && !viper_scpi_is_space(s[hlen])) {
        hlen++;
    }
    if (hlen == 0 || s[0] == '*') {
        return false;
    }
    {
        /* Parameters before the block: drop the ',' that precedes it. */
        size_t pn = n;
        while (pn > hlen && viper_scpi_is_space(s[pn - 1])) {
            pn--;
        }
        if (pn > hlen && s[pn - 1] == ',') {
            pn--;
        }
        ctx->param_count = 0;
        ctx->is_query = 0;
        ctx->prefix_count = 0; /* first command of a message: no inherited path */
        if (split_params(ctx, s + hlen, pn - hlen) != VIPER_SCPI_RES_OK) {
            return false;
        }
    }
    sn = (resolve_header(ctx, s, hlen, &node) == VIPER_SCPI_RES_OK)
             ? find_stream_node(ctx, node) : NULL;
    if (sn == NULL) {
        ctx->prefix_count = 0;
        return false;
    }

    /* The message starts executing now. */
    if (queued(ctx) && viper_scpi_output_pending(ctx)) {
        output_reset(ctx);
        viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_QUERY_INTERRUPTED);
    }
    ctx->line_has_output = 0; /* message_begin(), except the path: */
    ctx->out_overflow = 0;    /* resolve_header() has just set it */
    ctx->msg_started = 1;
    ctx->exec_pos = (size_t)(s - ctx->buffer) + hlen; /* end of the header */
    ctx->unit_has_output = 0;
    ctx->stream = sn->handler;
    ctx->block_state = BLK_STREAM;
#if SIZE_MAX < 0xFFFFFFFFu
    if (ctx->block_remaining > SIZE_MAX) {
        /* 16-bit size_t: BEGIN could not report the length. */
        viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_OUT_OF_RANGE);
        ctx->block_state = BLK_SKIP;
    } else
#endif
    if (!stream_call(ctx, VIPER_SCPI_STREAM_BEGIN, NULL, (size_t)ctx->block_remaining)) {
        ctx->block_state = BLK_SKIP;
    }
    if (ctx->block_remaining == 0) {
        ctx->skip_unit = (ctx->block_state == BLK_SKIP) ? 1 : 0;
        ctx->block_state = ctx->skip_unit ? BLK_NONE : BLK_TAIL;
    }
    return true;
}

/* Block bytes of a streamed (or skipped) block. */
static void stream_data(viper_scpi_context_t *ctx, const char *p, size_t k)
{
    if (ctx->block_state == BLK_STREAM &&
        !stream_call(ctx, VIPER_SCPI_STREAM_DATA, (const uint8_t *)p, k)) {
        ctx->block_state = BLK_SKIP;
    }
    ctx->block_remaining -= (uint32_t)k;
    if (ctx->block_remaining == 0) {
        /* A failed block still has to have its unit consumed. */
        ctx->skip_unit = (ctx->block_state == BLK_SKIP) ? 1 : 0;
        ctx->block_state = ctx->skip_unit ? BLK_NONE : BLK_TAIL;
    }
}

/*
 * A character after a streamed block. White space, then ';' or the message
 * end, complete the command; returns true for those. Anything else starts a
 * parameter that is not allowed: the handler gets ABORT, and the rest of the
 * unit is lexed like any other input (so strings and blocks in it cannot end
 * it early) but not kept. Returns false so that @p c itself is lexed too.
 */
static bool stream_tail(viper_scpi_context_t *ctx, char c)
{
    if (is_terminator(ctx, c)) {
        finish_line(ctx);
    } else if (c == ';' || c == '\n') {
        stream_unit_done(ctx);
    } else if (!viper_scpi_is_space(c)) {
        stream_abort(ctx);
        viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_PARAM_NOT_ALLOWED);
        ctx->block_state = BLK_NONE;
        ctx->skip_unit = 1;
        return false;
    }
    return true;
}

/* A digit of a block header: "#0" (indefinite), or "#<n>" and its n length
 * digits (definite). */
static void block_digit(viper_scpi_context_t *ctx, char c)
{
    store(ctx, c);
    ctx->last = c;
    if (ctx->block_state == BLK_HASH) {
        if (c == '0') {
            ctx->block_state = BLK_INDEFINITE;
        } else {
            ctx->block_state = BLK_LENGTH;
            ctx->block_digits = (uint8_t)(c - '0');
            ctx->block_remaining = 0;
        }
        return;
    }
    ctx->block_remaining = ctx->block_remaining * 10u + (uint32_t)(c - '0');
    if (--ctx->block_digits == 0 && !stream_start(ctx)) {
        ctx->block_state = (ctx->block_remaining > 0) ? BLK_DATA : BLK_NONE;
    }
}

/*
 * The one framing state machine: it decides where the program message ends,
 * which bytes are strings and blocks, and where units end, for every byte --
 * also for bytes it does not keep (an overrun message, a message refused
 * while another is suspended, the rest of a failed unit after a streamed
 * block). run_line() and split_params() apply the same rules to the stored
 * line: quotes, and blocks that start at a parameter boundary.
 */
void viper_scpi_input_char(viper_scpi_context_t *ctx, char c)
{
    if (ctx->wait != VIPER_SCPI_WAIT_NONE && !ctx->overflow && c != '\n' &&
        !viper_scpi_is_space(c)) {
        /* Refused (viper_scpi_busy()): the whole message is dropped, and
         * reported as -363 at its end. */
        ctx->overflow = 1;
    }
    switch (ctx->block_state) {
    case BLK_STREAM:
    case BLK_SKIP:
        stream_data(ctx, &c, 1);
        return;
    case BLK_TAIL:
        if (stream_tail(ctx, c)) {
            return;
        }
        break;
    case BLK_DATA:
        store(ctx, c);
        ctx->last = c;
        if (--ctx->block_remaining == 0) {
            ctx->block_state = BLK_NONE;
        }
        return;
    case BLK_INDEFINITE:
        /* IEEE 488.2 7.7.6.2: the block ends with the message, at NL^END. On
         * a transport that signals the end out of band (EOM / END) a bare LF
         * is block data. */
        if (c == '\n' && !queued(ctx)) {
            finish_line(ctx);
        } else {
            store(ctx, c);
            ctx->last = c;
        }
        return;
    case BLK_HASH:
    case BLK_LENGTH:
        if (c >= '0' && c <= '9') {
            block_digit(ctx, c);
            return;
        }
        if (is_terminator(ctx, c)) {
            finish_line(ctx); /* message ends inside the block header: -161 */
            return;
        }
        if (c == '\r' || c == '\0') {
            return; /* "#\r\n" and "#1\r\n" end like "#\n" and "#1\n" */
        }
        /* "#H..." etc. is ordinary text; an incomplete definite block
         * ("#1;", "#2x") is reported by split_params(). */
        ctx->block_state = BLK_NONE;
        break;
    default:
        break;
    }

    /* In the message-exchange model the message end is signalled out of band
     * (EOM / END), so a newline inside an open quote is string data (IEEE
     * 488.2 7.7.5: any 7-bit ASCII character may appear between the
     * delimiters). On stream transports LF stays the only terminator, even
     * inside quotes, so one lost quote cannot swallow every following
     * message. */
    if (ctx->quote != 0 && queued(ctx) && c == '\n') {
        store(ctx, c);
        ctx->last = c;
        return;
    }
    if (is_terminator(ctx, c)) {
        finish_line(ctx);
        return;
    }
    if (ctx->quote != 0) {
        if (c == ctx->quote) {
            ctx->quote = 0;
        }
    } else if (c == '"' || c == '\'') {
        ctx->quote = c;
    } else if (c == '#' && param_boundary(ctx->last)) {
        ctx->block_state = BLK_HASH;
        ctx->hash_pos = ctx->length;
    }
    store(ctx, c);
    ctx->last = c;
    if ((c == ';' || c == '\n') && ctx->quote == 0) {
        if (ctx->skip_unit) {
            stream_unit_done(ctx); /* the failed unit after a streamed block */
        } else if (!ctx->overflow && ctx->wait == VIPER_SCPI_WAIT_NONE) {
            ctx->unit_start = ctx->length;
        }
    }
}

size_t viper_scpi_input_until_busy(viper_scpi_context_t *ctx, const char *data, size_t len)
{
    size_t i = 0;

    while (i < len) {
        if (ctx->block_state == BLK_STREAM || ctx->block_state == BLK_SKIP) {
            /* Hand streamed block data over in as large a piece as arrived. */
            size_t k = len - i;
            if (k > ctx->block_remaining) {
                k = ctx->block_remaining;
            }
            stream_data(ctx, data + i, k);
            i += k;
        } else {
            if (ctx->wait != VIPER_SCPI_WAIT_NONE) {
                break; /* busy: the caller holds the rest back */
            }
            viper_scpi_input_char(ctx, data[i++]);
        }
    }
    return i;
}

void viper_scpi_input_buffer(viper_scpi_context_t *ctx, const char *data, size_t len)
{
    size_t i = viper_scpi_input_until_busy(ctx, data, len);

    /* Bytes that arrive while busy are refused: their messages are dropped
     * whole and reported as -363. */
    while (i < len) {
        viper_scpi_input_char(ctx, data[i++]);
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
