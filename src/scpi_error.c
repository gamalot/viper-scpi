/* SCPI error queue, status byte helpers and the built-in SYSTem subtree. */
#include "scpi_internal.h"

static const struct {
    int16_t code;
    const char *text;
} error_texts[] = {
    { 0, "No error" },
    { VIPER_SCPI_RES_ERR_UNKNOWN_CMD, "Command error" },
    { VIPER_SCPI_RES_ERR_SYNTAX, "Syntax error" },
    { VIPER_SCPI_RES_ERR_DATA_TYPE, "Data type error" },
    { VIPER_SCPI_RES_ERR_PARAM_NOT_ALLOWED, "Parameter not allowed" },
    { VIPER_SCPI_RES_ERR_MISSING_PARAM, "Missing parameter" },
    { VIPER_SCPI_RES_ERR_MNEMONIC_TOO_LONG, "Program mnemonic too long" },
    { VIPER_SCPI_RES_ERR_UNDEFINED_HEADER, "Undefined header" },
    { VIPER_SCPI_RES_ERR_EXECUTION, "Execution error" },
    { VIPER_SCPI_RES_ERR_PARAM_INVALID, "Parameter error" },
    { VIPER_SCPI_RES_ERR_OUT_OF_RANGE, "Data out of range" },
    { VIPER_SCPI_RES_ERR_ILLEGAL_VALUE, "Illegal parameter value" },
    { VIPER_SCPI_RES_ERR_QUEUE_OVERFLOW, "Queue overflow" },
    { VIPER_SCPI_RES_ERR_INPUT_OVERRUN, "Input buffer overrun" },
};

const char *viper_scpi_error_message(int code)
{
    for (size_t i = 0; i < sizeof error_texts / sizeof error_texts[0]; i++) {
        if (error_texts[i].code == code) {
            return error_texts[i].text;
        }
    }
    return "Device-specific error";
}

void viper_scpi_event_set(viper_scpi_context_t *ctx, uint8_t mask)
{
    ctx->esr = (uint8_t)(ctx->esr | mask);
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

void viper_scpi_error_push(viper_scpi_context_t *ctx, int code)
{
    if (code == 0) {
        return;
    }
    viper_scpi_event_set(ctx, esr_bit_for(code));

    if (ctx->err_count >= VIPER_SCPI_ERROR_QUEUE_SIZE) {
        /* Queue full: the newest entry becomes "Queue overflow". */
        size_t last = ((size_t)ctx->err_head + ctx->err_count - 1) % VIPER_SCPI_ERROR_QUEUE_SIZE;
        ctx->err_queue[last] = VIPER_SCPI_RES_ERR_QUEUE_OVERFLOW;
        return;
    }
    ctx->err_queue[((size_t)ctx->err_head + ctx->err_count) % VIPER_SCPI_ERROR_QUEUE_SIZE] =
        (int16_t)code;
    ctx->err_count++;
}

int viper_scpi_error_pop(viper_scpi_context_t *ctx)
{
    int code;

    if (ctx->err_count == 0) {
        return 0;
    }
    code = ctx->err_queue[ctx->err_head];
    ctx->err_head = (uint8_t)((ctx->err_head + 1) % VIPER_SCPI_ERROR_QUEUE_SIZE);
    ctx->err_count--;
    return code;
}

size_t viper_scpi_error_count(const viper_scpi_context_t *ctx)
{
    return ctx->err_count;
}

void viper_scpi_error_clear(viper_scpi_context_t *ctx)
{
    ctx->err_head = 0;
    ctx->err_count = 0;
}

uint8_t viper_scpi_status_byte(const viper_scpi_context_t *ctx)
{
    uint8_t stb = 0;

    if (ctx->err_count > 0) {
        stb |= VIPER_SCPI_STB_EAV;
    }
    if ((ctx->esr & ctx->ese) != 0) {
        stb |= VIPER_SCPI_STB_ESB;
    }
    if ((stb & ctx->sre & ~VIPER_SCPI_STB_MSS) != 0) {
        stb |= VIPER_SCPI_STB_MSS;
    }
    return stb;
}

/* ---- Built-in SYSTem subtree ------------------------------------------------- */

static viper_scpi_result_t cmd_syst_err_next(viper_scpi_context_t *ctx)
{
    int code;
    viper_scpi_result_t res = viper_scpi_no_params(ctx);

    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (!viper_scpi_is_query(ctx)) {
        return VIPER_SCPI_RES_ERR_UNDEFINED_HEADER;
    }
    code = viper_scpi_error_pop(ctx);
    viper_scpi_result_int(ctx, code);
    viper_scpi_result_string(ctx, viper_scpi_error_message(code));
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_syst_err_count(viper_scpi_context_t *ctx)
{
    viper_scpi_result_t res = viper_scpi_no_params(ctx);

    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (!viper_scpi_is_query(ctx)) {
        return VIPER_SCPI_RES_ERR_UNDEFINED_HEADER;
    }
    viper_scpi_result_int(ctx, (int32_t)viper_scpi_error_count(ctx));
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_syst_version(viper_scpi_context_t *ctx)
{
    viper_scpi_result_t res = viper_scpi_no_params(ctx);

    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (!viper_scpi_is_query(ctx)) {
        return VIPER_SCPI_RES_ERR_UNDEFINED_HEADER;
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
    VIPER_SCPI_NODE_END
};
