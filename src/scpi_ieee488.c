/* IEEE 488.2 mandatory common commands. */
#include "scpi_internal.h"

static viper_scpi_result_t cmd_idn(viper_scpi_context_t *ctx)
{
    const viper_scpi_config_t *cfg = ctx->cfg;
    const char *manufacturer = cfg->idn_manufacturer ? cfg->idn_manufacturer
                                                      : VIPER_SCPI_DEFAULT_MANUFACTURER;
    const char *model = cfg->idn_model ? cfg->idn_model : VIPER_SCPI_DEFAULT_MODEL;
    const char *serial = cfg->idn_serial ? cfg->idn_serial : VIPER_SCPI_DEFAULT_SERIAL;
    const char *firmware = cfg->idn_firmware ? cfg->idn_firmware : VIPER_SCPI_DEFAULT_FIRMWARE;

    viper_scpi_result_text(ctx, manufacturer);
    viper_scpi_result_text(ctx, model);
    viper_scpi_result_text(ctx, serial);
    viper_scpi_result_text(ctx, firmware);
    ctx->indefinite = 1; /* arbitrary ASCII response data: last in the reply */
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_rst(viper_scpi_context_t *ctx)
{
    ctx->opc_armed = 0; /* Operation Complete Command Idle State */
    if (ctx->cfg->reset != NULL) {
        ctx->cfg->reset(ctx);
    }
    return VIPER_SCPI_RES_OK;
}

/* *TRG exists only if the instrument can be triggered (488.2 DT1). */
static viper_scpi_result_t cmd_trg(viper_scpi_context_t *ctx)
{
    if (ctx->cfg->trigger == NULL) {
        return VIPER_SCPI_RES_ERR_UNDEFINED_HEADER;
    }
    ctx->cfg->trigger(ctx);
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_cls(viper_scpi_context_t *ctx)
{
    viper_scpi_error_clear(ctx);
    ctx->esr = 0;
    ctx->opc_armed = 0; /* back to Operation Complete Command Idle State */
    ctx->status[VIPER_SCPI_STATUS_OPER].event = 0; /* SCPI: *CLS clears the */
    ctx->status[VIPER_SCPI_STATUS_QUES].event = 0; /* status event registers */
    viper_scpi_srq_update(ctx); /* so that a rise in the hook is a new request */
    if (ctx->cfg->clear != NULL) {
        ctx->cfg->clear(ctx);
    }
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_opc(viper_scpi_context_t *ctx)
{
    bool pending = viper_scpi_op_pending(ctx);

    if (viper_scpi_is_query(ctx)) {
        if (pending) {
            ctx->wait = VIPER_SCPI_WAIT_OPCQ; /* "1" is written on resume */
        } else {
            viper_scpi_result_int(ctx, 1);
        }
    } else if (pending) {
        ctx->opc_armed = 1; /* OPC is set by the last viper_scpi_op_end() */
    } else {
        viper_scpi_event_set(ctx, VIPER_SCPI_ESR_OPC);
    }
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_wai(viper_scpi_context_t *ctx)
{
    if (viper_scpi_op_pending(ctx)) {
        ctx->wait = VIPER_SCPI_WAIT_WAI; /* resumed by viper_scpi_poll() */
    }
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_stb(viper_scpi_context_t *ctx)
{
    viper_scpi_result_int(ctx, viper_scpi_status_byte(ctx));
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_esr(viper_scpi_context_t *ctx)
{
    uint8_t esr = ctx->esr;

    viper_scpi_result_int(ctx, esr);
    if (viper_scpi_reply_ok(ctx)) {
        ctx->esr = (uint8_t)(ctx->esr & ~esr); /* reading clears what it reported */
    }
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_tst(viper_scpi_context_t *ctx)
{
    viper_scpi_result_int(ctx, ctx->cfg->self_test != NULL ? ctx->cfg->self_test(ctx)
                                                           : 0); /* 0 = passed */
    return VIPER_SCPI_RES_OK;
}

/* Shared body of *ESE and *SRE: query the register or set it from 0..255. */
static viper_scpi_result_t reg_access(viper_scpi_context_t *ctx, uint8_t *reg)
{
    int32_t v;
    viper_scpi_result_t res;

    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_int(ctx, *reg);
        return VIPER_SCPI_RES_OK;
    }
    res = viper_scpi_param_register(ctx, 255, &v);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    *reg = (uint8_t)v;
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_ese(viper_scpi_context_t *ctx)
{
    return reg_access(ctx, &ctx->ese);
}

static viper_scpi_result_t cmd_sre(viper_scpi_context_t *ctx)
{
    /* Bit 6 (MSS/RQS) of the enable register is not used. */
    viper_scpi_result_t res = reg_access(ctx, &ctx->sre);

    ctx->sre &= (uint8_t)~VIPER_SCPI_STB_MSS;
    return res;
}

enum { KIND_QUERY = 1, KIND_COMMAND = 2, KIND_BOTH = 3 };

static const struct {
    const char *name;
    viper_scpi_cmd_callback_t handler;
    uint8_t kind;
    bool takes_params;
} common_cmds[] = {
    { "IDN", cmd_idn, KIND_QUERY, false },
    { "RST", cmd_rst, KIND_COMMAND, false },
    { "CLS", cmd_cls, KIND_COMMAND, false },
    { "OPC", cmd_opc, KIND_BOTH, false },
    { "WAI", cmd_wai, KIND_COMMAND, false },
    { "STB", cmd_stb, KIND_QUERY, false },
    { "ESR", cmd_esr, KIND_QUERY, false },
    { "ESE", cmd_ese, KIND_BOTH, true },
    { "SRE", cmd_sre, KIND_BOTH, true },
    { "TST", cmd_tst, KIND_QUERY, false },
    { "TRG", cmd_trg, KIND_COMMAND, false },
};

viper_scpi_result_t viper_scpi_ieee488_dispatch(viper_scpi_context_t *ctx,
                                                const char *name, size_t len)
{
    /* The instrument's own table first: it may add or replace commands. */
    for (const viper_scpi_node_t *n = ctx->cfg->common; n != NULL && n->pattern != NULL; n++) {
        if (n->callback != NULL && viper_scpi_pattern_match(n->pattern, name, len)) {
            return viper_scpi_call_command(ctx, n->callback);
        }
    }
    for (size_t i = 0; i < sizeof common_cmds / sizeof common_cmds[0]; i++) {
        /* The names are all upper case, so only the full name matches. */
        if (!viper_scpi_pattern_match(common_cmds[i].name, name, len)) {
            continue;
        }
        {
            /* *ESE and *SRE take a value; no query takes one. */
            int set = (common_cmds[i].kind & KIND_COMMAND) == 0 ? VIPER_SCPI_NO_FORM
                      : common_cmds[i].takes_params ? 1 : 0;
            int qry = (common_cmds[i].kind & KIND_QUERY) == 0 ? VIPER_SCPI_NO_FORM : 0;
            viper_scpi_result_t res = viper_scpi_expect(ctx, set, qry);
            if (res != VIPER_SCPI_RES_OK) {
                return res;
            }
        }
        return common_cmds[i].handler(ctx);
    }
    return VIPER_SCPI_RES_ERR_UNDEFINED_HEADER;
}
