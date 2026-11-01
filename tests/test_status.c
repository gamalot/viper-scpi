/* SCPI STATus subsystem: OPERation / QUEStionable, transitions, summaries. */
#include "test_util.h"

static const char *ask(const char *q)
{
    fx_clear();
    fx_send(q);
    return fx_out();
}

static void test_power_on_and_queries(void)
{
    fx_setup();
    CHECK_STR(ask("STAT:OPER?"), "0\n");
    CHECK_STR(ask("STAT:OPER:EVEN?"), "0\n");
    CHECK_STR(ask("STATUS:OPERATION:CONDITION?"), "0\n");
    CHECK_STR(ask("STAT:OPER:ENAB?"), "0\n");
    CHECK_STR(ask("STAT:OPER:PTR?"), "32767\n");   /* all ones */
    CHECK_STR(ask("STAT:OPER:NTR?"), "0\n");
    CHECK_STR(ask("STAT:QUES?"), "0\n");
    CHECK_STR(ask("STAT:QUES:COND?"), "0\n");
    CHECK_STR(ask("STAT:QUES:ENAB?"), "0\n");
    CHECK_STR(ask("STAT:QUES:PTR?"), "32767\n");
    CHECK_STR(ask("STAT:QUES:NTR?"), "0\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_set_and_errors(void)
{
    fx_setup();
    CHECK_STR(ask("STAT:OPER:ENAB 16;ENAB?"), "16\n");
    CHECK_STR(ask("STAT:QUES:PTR #H00FF;PTR?;:STAT:QUES:NTR 3;NTR?"), "255;3\n");
    CHECK_STR(ask("STAT:OPER:ENAB 32767;ENAB?"), "32767\n");
    fx_send("STAT:OPER:ENAB 32768");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -222);   /* bit 15 does not exist */
    fx_send("STAT:OPER:ENAB -1");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -222);
    fx_send("STAT:OPER:ENAB");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -109);
    fx_send("STAT:OPER:ENAB 1,2");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -108);
    fx_send("STAT:OPER:COND 1");                      /* read-only */
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("STAT:OPER 1");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("STAT:OPER:ENAB? 1");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -108);
    fx_send("STAT:PRES?");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    CHECK_STR(ask("STAT:OPER:ENAB?"), "32767\n");     /* failures changed nothing */
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_transitions(void)
{
    const viper_scpi_status_set_t *oper;

    fx_setup();
    oper = viper_scpi_status_get(&g_ctx, VIPER_SCPI_STATUS_OPER);

    /* Default filters: rising edges latch, falling ones do not. */
    viper_scpi_status_condition(&g_ctx, VIPER_SCPI_STATUS_OPER, 0x0010, 0x0010);
    CHECK_INT(oper->condition, 0x0010);
    CHECK_STR(ask("STAT:OPER:COND?"), "16\n");
    CHECK_STR(ask("STAT:OPER?"), "16\n");
    CHECK_STR(ask("STAT:OPER?"), "0\n");             /* cleared by reading */
    viper_scpi_status_condition(&g_ctx, VIPER_SCPI_STATUS_OPER, 0x0010, 0);
    CHECK_STR(ask("STAT:OPER?"), "0\n");
    CHECK_STR(ask("STAT:OPER:COND?"), "0\n");

    /* Negative filter only: the falling edge latches, the rising one not. */
    fx_send("STAT:OPER:PTR 0;NTR 16");
    viper_scpi_status_condition(&g_ctx, VIPER_SCPI_STATUS_OPER, 0x0010, 0x0010);
    CHECK_STR(ask("STAT:OPER?"), "0\n");
    viper_scpi_status_condition(&g_ctx, VIPER_SCPI_STATUS_OPER, 0x0010, 0);
    CHECK_STR(ask("STAT:OPER?"), "16\n");

    /* Both directions; mask leaves other bits alone; bit 15 is dropped. */
    fx_send("STAT:OPER:PTR 32767;NTR 32767");
    viper_scpi_status_condition(&g_ctx, VIPER_SCPI_STATUS_OPER, 0xFFFF, 0x8003);
    CHECK_INT(oper->condition, 0x0003);
    viper_scpi_status_condition(&g_ctx, VIPER_SCPI_STATUS_OPER, 0x0001, 0);
    CHECK_INT(oper->condition, 0x0002);
    CHECK_STR(ask("STAT:OPER?"), "3\n");

    /* No change, no event. */
    viper_scpi_status_condition(&g_ctx, VIPER_SCPI_STATUS_OPER, 0x0002, 0x0002);
    CHECK_STR(ask("STAT:OPER?"), "0\n");

    /* An invalid register selector is ignored. */
    viper_scpi_status_condition(&g_ctx, (viper_scpi_status_reg_t)7, 1, 1);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_summary_bits_and_srq(void)
{
    fx_setup();
    /* QUES event, not enabled: no summary. */
    viper_scpi_status_condition(&g_ctx, VIPER_SCPI_STATUS_QUES, 0x0200, 0x0200);
    CHECK_INT(viper_scpi_status_byte(&g_ctx), 0);
    CHECK(!viper_scpi_take_service_request(&g_ctx));

    /* Enabling it raises bit 3; with *SRE 8 that is a service request. */
    fx_send("*SRE 8;STAT:QUES:ENAB 512");
    CHECK_INT(viper_scpi_status_byte(&g_ctx), VIPER_SCPI_STB_QUES | VIPER_SCPI_STB_MSS);
    CHECK(viper_scpi_take_service_request(&g_ctx));
    CHECK_STR(ask("*STB?"), "72\n");
    CHECK_STR(ask("STAT:QUES?"), "512\n");             /* read clears the event */
    CHECK_INT(viper_scpi_status_byte(&g_ctx), 0);

    /* A later QUES event is a new edge. */
    viper_scpi_status_condition(&g_ctx, VIPER_SCPI_STATUS_QUES, 0x0200, 0);
    viper_scpi_status_condition(&g_ctx, VIPER_SCPI_STATUS_QUES, 0x0200, 0x0200);
    CHECK(viper_scpi_take_service_request(&g_ctx));

    /* OPER summary in bit 7. */
    fx_send("*SRE 128;STAT:OPER:ENAB 16");
    viper_scpi_status_condition(&g_ctx, VIPER_SCPI_STATUS_OPER, 0x0010, 0x0010);
    CHECK(viper_scpi_take_service_request(&g_ctx));
    CHECK_INT(viper_scpi_status_byte(&g_ctx) & VIPER_SCPI_STB_OPER, VIPER_SCPI_STB_OPER);
    CHECK_STR(ask("*STB?"), "200\n");                  /* OPER + MSS + QUES */
}

static void test_cls_and_preset(void)
{
    fx_setup();
    fx_send("STAT:OPER:ENAB 1;:STAT:QUES:ENAB 1;:STAT:QUES:NTR 1");
    viper_scpi_status_condition(&g_ctx, VIPER_SCPI_STATUS_OPER, 1, 1);
    viper_scpi_status_condition(&g_ctx, VIPER_SCPI_STATUS_QUES, 1, 1);

    /* *CLS clears events, not conditions or enables. */
    fx_send("*CLS");
    CHECK_STR(ask("STAT:OPER?;:STAT:QUES?"), "0;0\n");
    CHECK_STR(ask("STAT:OPER:COND?;ENAB?"), "1;1\n");
    CHECK_INT(viper_scpi_status_byte(&g_ctx), 0);

    /* STAT:PRES restores enables and filters, leaves events and conditions. */
    viper_scpi_status_condition(&g_ctx, VIPER_SCPI_STATUS_QUES, 1, 0);  /* NTR=1: event */
    fx_send("STAT:PRES");
    CHECK_STR(ask("STAT:QUES:ENAB?;PTR?;NTR?"), "0;32767;0\n");
    CHECK_STR(ask("STAT:OPER:ENAB?"), "0\n");
    CHECK_STR(ask("STAT:QUES?"), "1\n");
    CHECK_STR(ask("STAT:OPER:COND?"), "1\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

int main(void)
{
    test_power_on_and_queries();
    test_set_and_errors();
    test_transitions();
    test_summary_bits_and_srq();
    test_cls_and_preset();
    return test_summary("test_status");
}
