/* SCPI STATus subsystem: OPERation and QUEStionable register sets. */
#include "scpi_internal.h"

#define STATUS_BITS 0x7FFFu /* bit 15 of a SCPI status register is always 0 */

void viper_scpi_status_preset(viper_scpi_context_t *ctx)
{
    for (size_t i = 0; i < 2; i++) {
        ctx->status[i].enable = 0;
        ctx->status[i].ptr = STATUS_BITS;
        ctx->status[i].ntr = 0;
    }
    viper_scpi_srq_update(ctx);
}

void viper_scpi_status_condition(viper_scpi_context_t *ctx, viper_scpi_status_reg_t reg,
                                 uint16_t mask, uint16_t value)
{
    viper_scpi_status_set_t *set;
    uint16_t old_cond;
    uint16_t new_cond;
    uint16_t rising;
    uint16_t falling;

    if ((unsigned)reg > VIPER_SCPI_STATUS_QUES) {
        return;
    }
    set = &ctx->status[reg];
    old_cond = set->condition;
    new_cond = (uint16_t)(((old_cond & ~mask) | (value & mask)) & STATUS_BITS);
    rising = (uint16_t)(new_cond & ~old_cond);
    falling = (uint16_t)(old_cond & ~new_cond);
    set->condition = new_cond;
    set->event = (uint16_t)(set->event | (rising & set->ptr) | (falling & set->ntr));
    viper_scpi_srq_update(ctx);
}

const viper_scpi_status_set_t *viper_scpi_status_get(const viper_scpi_context_t *ctx,
                                                     viper_scpi_status_reg_t reg)
{
    return &ctx->status[(unsigned)reg > VIPER_SCPI_STATUS_QUES ? 0 : reg];
}

/* ---- Built-in commands --------------------------------------------------------- */

enum { FIELD_EVENT, FIELD_CONDITION, FIELD_ENABLE, FIELD_PTR, FIELD_NTR };

/* Shared body: query a field (EVENt clears on read) or set a writable one. */
static viper_scpi_result_t status_access(viper_scpi_context_t *ctx, unsigned reg, int field)
{
    viper_scpi_status_set_t *set = &ctx->status[reg];
    uint16_t *target = NULL;
    int32_t v;
    viper_scpi_result_t res;

    switch (field) {
    case FIELD_ENABLE: target = &set->enable; break;
    case FIELD_PTR: target = &set->ptr; break;
    case FIELD_NTR: target = &set->ntr; break;
    default: break;
    }

    /* EVENt and CONDition are queries; the others set a register too. */
    res = viper_scpi_expect(ctx, target != NULL ? 1 : VIPER_SCPI_NO_FORM, 0);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (viper_scpi_is_query(ctx)) {
        if (field == FIELD_EVENT) {
            uint16_t event = set->event;

            viper_scpi_result_int(ctx, event);
            if (viper_scpi_reply_ok(ctx)) {
                set->event = (uint16_t)(set->event & ~event); /* read clears it */
                viper_scpi_srq_update(ctx);
            }
        } else if (field == FIELD_CONDITION) {
            viper_scpi_result_int(ctx, set->condition);
        } else if (target != NULL) {
            viper_scpi_result_int(ctx, *target);
        }
        return VIPER_SCPI_RES_OK;
    }
    if (target == NULL) {
        return VIPER_SCPI_RES_ERR_UNDEFINED_HEADER; /* refused by expect() already */
    }

    res = viper_scpi_param_register(ctx, (int32_t)STATUS_BITS, &v);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    *target = (uint16_t)v;
    viper_scpi_srq_update(ctx); /* a new enable mask can raise the summary */
    return VIPER_SCPI_RES_OK;
}

#define STATUS_CMD(name, reg, field)                                          \
    static viper_scpi_result_t name(viper_scpi_context_t *ctx)                \
    {                                                                         \
        return status_access(ctx, reg, field);                                \
    }

STATUS_CMD(oper_event, VIPER_SCPI_STATUS_OPER, FIELD_EVENT)
STATUS_CMD(oper_cond, VIPER_SCPI_STATUS_OPER, FIELD_CONDITION)
STATUS_CMD(oper_enable, VIPER_SCPI_STATUS_OPER, FIELD_ENABLE)
STATUS_CMD(oper_ptr, VIPER_SCPI_STATUS_OPER, FIELD_PTR)
STATUS_CMD(oper_ntr, VIPER_SCPI_STATUS_OPER, FIELD_NTR)
STATUS_CMD(ques_event, VIPER_SCPI_STATUS_QUES, FIELD_EVENT)
STATUS_CMD(ques_cond, VIPER_SCPI_STATUS_QUES, FIELD_CONDITION)
STATUS_CMD(ques_enable, VIPER_SCPI_STATUS_QUES, FIELD_ENABLE)
STATUS_CMD(ques_ptr, VIPER_SCPI_STATUS_QUES, FIELD_PTR)
STATUS_CMD(ques_ntr, VIPER_SCPI_STATUS_QUES, FIELD_NTR)

static viper_scpi_result_t cmd_preset(viper_scpi_context_t *ctx)
{
    viper_scpi_result_t res = viper_scpi_expect(ctx, 0, VIPER_SCPI_NO_FORM);

    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    viper_scpi_status_preset(ctx);
    return VIPER_SCPI_RES_OK;
}

/* A bare STAT:OPER? is the event query, as STATus:OPERation[:EVENt]? says. */
static const viper_scpi_node_t oper_children[] = {
    { "[EVENt]", oper_event, NULL },
    { "CONDition", oper_cond, NULL },
    { "ENABle", oper_enable, NULL },
    { "PTRansition", oper_ptr, NULL },
    { "NTRansition", oper_ntr, NULL },
    VIPER_SCPI_NODE_END
};

static const viper_scpi_node_t ques_children[] = {
    { "[EVENt]", ques_event, NULL },
    { "CONDition", ques_cond, NULL },
    { "ENABle", ques_enable, NULL },
    { "PTRansition", ques_ptr, NULL },
    { "NTRansition", ques_ntr, NULL },
    VIPER_SCPI_NODE_END
};

const viper_scpi_node_t viper_scpi_status_children[] = {
    { "OPERation", NULL, oper_children },
    { "QUEStionable", NULL, ques_children },
    { "PRESet", cmd_preset, NULL },
    VIPER_SCPI_NODE_END
};
