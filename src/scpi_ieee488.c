/* IEEE 488.2 mandatory common commands. */
#include <string.h>

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
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_rst(viper_scpi_context_t *ctx)
{
    if (ctx->cfg->reset != NULL) {
        ctx->cfg->reset(ctx);
    }
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_cls(viper_scpi_context_t *ctx)
{
    viper_scpi_error_clear(ctx);
    ctx->esr = 0;
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_opc(viper_scpi_context_t *ctx)
{
    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_int(ctx, 1);
    } else {
        viper_scpi_event_set(ctx, VIPER_SCPI_ESR_OPC);
    }
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_wai(viper_scpi_context_t *ctx)
{
    (void)ctx; /* Commands run synchronously, so nothing is ever pending. */
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_stb(viper_scpi_context_t *ctx)
{
    viper_scpi_result_int(ctx, viper_scpi_status_byte(ctx));
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_esr(viper_scpi_context_t *ctx)
{
    viper_scpi_result_int(ctx, ctx->esr);
    ctx->esr = 0;
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_tst(viper_scpi_context_t *ctx)
{
    viper_scpi_result_int(ctx, 0); /* 0 = self test passed */
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
    res = viper_scpi_param_int32(ctx, 0, &v);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (v < 0 || v > 255) {
        return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
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
    uint8_t kind;
    viper_scpi_cmd_callback_t handler;
    bool takes_params;
} common_cmds[] = {
    { "IDN", KIND_QUERY, cmd_idn, false },
    { "RST", KIND_COMMAND, cmd_rst, false },
    { "CLS", KIND_COMMAND, cmd_cls, false },
    { "OPC", KIND_BOTH, cmd_opc, false },
    { "WAI", KIND_COMMAND, cmd_wai, false },
    { "STB", KIND_QUERY, cmd_stb, false },
    { "ESR", KIND_QUERY, cmd_esr, false },
    { "ESE", KIND_BOTH, cmd_ese, true },
    { "SRE", KIND_BOTH, cmd_sre, true },
    { "TST", KIND_QUERY, cmd_tst, false },
};

viper_scpi_result_t viper_scpi_ieee488_dispatch(viper_scpi_context_t *ctx,
                                                const char *name, size_t len,
                                                bool query)
{
    for (size_t i = 0; i < sizeof common_cmds / sizeof common_cmds[0]; i++) {
        if (strlen(common_cmds[i].name) != len ||
            !viper_scpi_pattern_match(common_cmds[i].name, name, len)) {
            continue;
        }
        if ((common_cmds[i].kind & (query ? KIND_QUERY : KIND_COMMAND)) == 0) {
            break;
        }
        /* *ESE and *SRE require a value; a query takes none. */
        if (!common_cmds[i].takes_params || query) {
            viper_scpi_result_t res = viper_scpi_no_params(ctx);
            if (res != VIPER_SCPI_RES_OK) {
                return res;
            }
        }
        return common_cmds[i].handler(ctx);
    }
    return VIPER_SCPI_RES_ERR_UNDEFINED_HEADER;
}
