/* SCPI error queue, status byte helpers and the built-in SYSTem subtree. */
#include "scpi_internal.h"

/* Sorted by code, descending, and grouped by class. Searched linearly: at
 * this size a search is not worth the code; the instrument's own texts are a
 * separate table (viper_scpi_config_t.error_texts). */
static const struct {
    int16_t code;
    const char *text;
} builtin_texts[] = {
    { 0, "No error" },
    { VIPER_SCPI_RES_ERR_UNKNOWN_CMD, "Command error" },
    { VIPER_SCPI_RES_ERR_SYNTAX, "Syntax error" },
    { VIPER_SCPI_RES_ERR_DATA_TYPE, "Data type error" },
    { VIPER_SCPI_RES_ERR_PARAM_NOT_ALLOWED, "Parameter not allowed" },
    { VIPER_SCPI_RES_ERR_MISSING_PARAM, "Missing parameter" },
    { VIPER_SCPI_RES_ERR_MNEMONIC_TOO_LONG, "Program mnemonic too long" },
    { VIPER_SCPI_RES_ERR_UNDEFINED_HEADER, "Undefined header" },
    { VIPER_SCPI_RES_ERR_SUFFIX_RANGE, "Header suffix out of range" },
    { VIPER_SCPI_RES_ERR_INVALID_SUFFIX, "Invalid suffix" },
    { VIPER_SCPI_RES_ERR_BLOCK_DATA, "Block data error" },
    { VIPER_SCPI_RES_ERR_INVALID_BLOCK, "Invalid block data" },
    { VIPER_SCPI_RES_ERR_EXECUTION, "Execution error" },
    { VIPER_SCPI_RES_ERR_PARAM_INVALID, "Parameter error" },
    { VIPER_SCPI_RES_ERR_OUT_OF_RANGE, "Data out of range" },
    { VIPER_SCPI_RES_ERR_ILLEGAL_VALUE, "Illegal parameter value" },
    { VIPER_SCPI_RES_ERR_OUT_OF_MEMORY, "Out of memory" },
    { VIPER_SCPI_RES_ERR_QUEUE_OVERFLOW, "Queue overflow" },
    { VIPER_SCPI_RES_ERR_INPUT_OVERRUN, "Input buffer overrun" },
    { VIPER_SCPI_RES_ERR_QUERY, "Query error" },
    { VIPER_SCPI_RES_ERR_QUERY_INTERRUPTED, "Query INTERRUPTED" },
    { VIPER_SCPI_RES_ERR_QUERY_UNTERMINATED, "Query UNTERMINATED" },
};

const char *viper_scpi_error_message(int code)
{
    for (size_t i = 0; i < sizeof builtin_texts / sizeof builtin_texts[0]; i++) {
        if (builtin_texts[i].code == code) {
            return builtin_texts[i].text;
        }
    }
    return "Device-specific error";
}

const char *viper_scpi_error_text(const viper_scpi_context_t *ctx, int code)
{
    for (const viper_scpi_error_text_t *t = ctx->cfg->error_texts; t != NULL && t->text != NULL;
         t++) {
        if (t->code == code) {
            return t->text;
        }
    }
    return viper_scpi_error_message(code);
}

void viper_scpi_event_set(viper_scpi_context_t *ctx, uint8_t mask)
{
    ctx->esr = (uint8_t)(ctx->esr | mask);
    viper_scpi_srq_update(ctx);
}

static uint8_t esr_bit_for(int code)
{
    if (code <= -100 && code >= -199) {
        return VIPER_SCPI_ESR_CME;
    }
    if (code <= -200 && code >= -299) {
        return VIPER_SCPI_ESR_EXE;
    }
    if (code <= -300 && code >= -399) {
        return VIPER_SCPI_ESR_DDE;
    }
    if (code <= -400 && code >= -499) {
        return VIPER_SCPI_ESR_QYE;
    }
    return code > 0 ? VIPER_SCPI_ESR_DDE : 0;
}

void viper_scpi_error_push_info(viper_scpi_context_t *ctx, int code, const char *info)
{
    if (code == 0) {
        return;
    }
    if (code < -32768 || code > 32767) {
        code = VIPER_SCPI_RES_ERR_EXECUTION; /* would not survive int16_t */
        info = NULL;
    }
    viper_scpi_event_set(ctx, esr_bit_for(code));

    if (ctx->err_count >= VIPER_SCPI_ERROR_QUEUE_SIZE) {
        /* Queue full: the newest entry becomes "Queue overflow". */
        size_t last = ((size_t)ctx->err_head + ctx->err_count - 1) % VIPER_SCPI_ERROR_QUEUE_SIZE;
        ctx->err_queue[last] = VIPER_SCPI_RES_ERR_QUEUE_OVERFLOW;
        ctx->err_info[last] = NULL;
        ctx->esr = (uint8_t)(ctx->esr | VIPER_SCPI_ESR_DDE); /* -350 is a -3xx error */
    } else {
        size_t slot = ((size_t)ctx->err_head + ctx->err_count) % VIPER_SCPI_ERROR_QUEUE_SIZE;
        ctx->err_queue[slot] = (int16_t)code;
        ctx->err_info[slot] = info;
        ctx->err_count++;
    }
    viper_scpi_srq_update(ctx); /* EAV, and ESB through the ESR bit */
}

void viper_scpi_error_push(viper_scpi_context_t *ctx, int code)
{
    viper_scpi_error_push_info(ctx, code, NULL);
}

viper_scpi_result_t viper_scpi_fail(viper_scpi_context_t *ctx, int code, const char *detail)
{
    viper_scpi_error_push_info(ctx, code, detail);
    return VIPER_SCPI_RES_QUEUED;
}

int viper_scpi_error_pop_info(viper_scpi_context_t *ctx, const char **info)
{
    int code;

    if (info != NULL) {
        *info = NULL;
    }
    if (ctx->err_count == 0) {
        return 0;
    }
    code = ctx->err_queue[ctx->err_head];
    if (info != NULL) {
        *info = ctx->err_info[ctx->err_head];
    }
    ctx->err_head = (uint8_t)((ctx->err_head + 1) % VIPER_SCPI_ERROR_QUEUE_SIZE);
    ctx->err_count--;
    viper_scpi_srq_update(ctx); /* EAV may fall */
    return code;
}

int viper_scpi_error_pop(viper_scpi_context_t *ctx)
{
    return viper_scpi_error_pop_info(ctx, NULL);
}

size_t viper_scpi_error_count(const viper_scpi_context_t *ctx)
{
    return ctx->err_count;
}

void viper_scpi_error_clear(viper_scpi_context_t *ctx)
{
    ctx->err_head = 0;
    ctx->err_count = 0;
    viper_scpi_srq_update(ctx); /* EAV falls */
}

uint8_t viper_scpi_status_byte(const viper_scpi_context_t *ctx)
{
    uint8_t stb = 0;

    if (ctx->err_count > 0) {
        stb |= VIPER_SCPI_STB_EAV;
    }
    /* MAV: a complete reply waits to be read -- or, for *STB? itself, the
     * running message has queued part of its reply (IEEE 488.2 sees the
     * output queue). A suspended message's partial reply stays hidden. */
    if (viper_scpi_output_pending(ctx) || (ctx->running && viper_scpi_output_queued(ctx))) {
        stb |= VIPER_SCPI_STB_MAV;
    }
    if ((ctx->esr & ctx->ese) != 0) {
        stb |= VIPER_SCPI_STB_ESB;
    }
    if ((ctx->status[VIPER_SCPI_STATUS_QUES].event &
         ctx->status[VIPER_SCPI_STATUS_QUES].enable) != 0) {
        stb |= VIPER_SCPI_STB_QUES;
    }
    if ((ctx->status[VIPER_SCPI_STATUS_OPER].event &
         ctx->status[VIPER_SCPI_STATUS_OPER].enable) != 0) {
        stb |= VIPER_SCPI_STB_OPER;
    }
    if ((stb & ctx->sre & ~VIPER_SCPI_STB_MSS) != 0) {
        stb |= VIPER_SCPI_STB_MSS;
    }
    return stb;
}

void viper_scpi_srq_update(viper_scpi_context_t *ctx)
{
    uint8_t stb = viper_scpi_status_byte(ctx);
    bool now;

    if (!viper_scpi_output_pending(ctx)) {
        /* A reply still being produced cannot be read yet: it must not raise
         * a request that a later *WAI or a -321 would leave unanswerable. */
        stb = (uint8_t)(stb & ~VIPER_SCPI_STB_MAV);
    }
    now = (stb & ctx->sre & ~VIPER_SCPI_STB_MSS) != 0;
    if (now && ctx->srq_prev == 0) {
        ctx->srq_new = 1;
    }
    if (!now) {
        /* The reason for the request is gone before anyone polled: the
         * device stops requesting service (IEEE 488.1 SRQS -> NPRS), and a
         * request not yet taken is no longer reported. */
        ctx->rqs = 0;
        ctx->srq_new = 0;
    }
    ctx->srq_prev = now ? 1u : 0u;
}

bool viper_scpi_take_service_request(viper_scpi_context_t *ctx)
{
    bool taken;

    viper_scpi_srq_update(ctx);
    taken = ctx->srq_new != 0;
    ctx->srq_new = 0;
    if (taken) {
        ctx->rqs = 1;
    }
    return taken;
}

uint8_t viper_scpi_serial_poll(viper_scpi_context_t *ctx)
{
    uint8_t stb;

    viper_scpi_srq_update(ctx);
    stb = (uint8_t)(viper_scpi_status_byte(ctx) & ~VIPER_SCPI_STB_MSS);
    if (ctx->rqs) {
        stb |= VIPER_SCPI_STB_RQS;
    }
    ctx->rqs = 0;
    return stb;
}

void viper_scpi_rqs_delivered(viper_scpi_context_t *ctx)
{
    ctx->rqs = 0;
}

/* ---- Built-in SYSTem subtree ------------------------------------------------- */

static viper_scpi_result_t cmd_syst_err_next(viper_scpi_context_t *ctx)
{
    int code;
    const char *info;
    viper_scpi_result_t res = viper_scpi_expect(ctx, VIPER_SCPI_NO_FORM, 0);

    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    code = viper_scpi_error_pop_info(ctx, &info);
    viper_scpi_result_int(ctx, code);
    viper_scpi_result_string_pair(ctx, viper_scpi_error_text(ctx, code), info);
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_syst_err_count(viper_scpi_context_t *ctx)
{
    viper_scpi_result_t res = viper_scpi_expect(ctx, VIPER_SCPI_NO_FORM, 0);

    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    viper_scpi_result_int(ctx, (int32_t)viper_scpi_error_count(ctx));
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_syst_version(viper_scpi_context_t *ctx)
{
    viper_scpi_result_t res = viper_scpi_expect(ctx, VIPER_SCPI_NO_FORM, 0);

    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    viper_scpi_result_text(ctx, "1999.0");
    return VIPER_SCPI_RES_OK;
}

static const viper_scpi_node_t syst_err_children[] = {
    { "NEXT", cmd_syst_err_next, NULL },
    { "COUNt", cmd_syst_err_count, NULL },
    VIPER_SCPI_NODE_END
};

static const viper_scpi_node_t syst_children[] = {
    { "ERRor", cmd_syst_err_next, syst_err_children },
    { "VERSion", cmd_syst_version, NULL },
    VIPER_SCPI_NODE_END
};

const viper_scpi_node_t viper_scpi_builtin_root[] = {
    { "SYSTem", NULL, syst_children },
    { "STATus", NULL, viper_scpi_status_children },
    VIPER_SCPI_NODE_END
};
