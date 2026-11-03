/* IEEE 488.2 mandatory commands and status reporting. */
#include "test_util.h"

static void test_idn(void)
{
    fx_setup();
    fx_send("*IDN?");
    CHECK_STR(fx_out(), "Acme,Widget,SN123,1.2.3\n");
    fx_clear();
    fx_send("*idn?");
    CHECK_STR(fx_out(), "Acme,Widget,SN123,1.2.3\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_idn_defaults(void)
{
    static const viper_scpi_config_t bare = { 0 };
    viper_scpi_context_t c;
    (void)bare;

    /* Default identity is used when the configuration leaves fields NULL;
     * a writer is needed to observe it. */
    fx_setup();
    {
        static viper_scpi_config_t cfg;
        cfg = *(g_ctx.cfg);
        cfg.idn_manufacturer = NULL;
        cfg.idn_model = NULL;
        cfg.idn_serial = NULL;
        cfg.idn_firmware = NULL;
        CHECK_INT(viper_scpi_init(&c, &cfg), 0);
        viper_scpi_input_buffer(&c, "*IDN?\n", 6);
        CHECK_STR(fx_out(), VIPER_SCPI_DEFAULT_MANUFACTURER "," VIPER_SCPI_DEFAULT_MODEL ","
                            VIPER_SCPI_DEFAULT_SERIAL "," VIPER_SCPI_DEFAULT_FIRMWARE "\n");
    }
}

static void test_rst(void)
{
    fx_setup();
    fx_send("*RST");
    CHECK_INT(g_reset_calls, 1);
    fx_send("*rst;*RST");
    CHECK_INT(g_reset_calls, 3);
    CHECK_STR(fx_out(), "");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_cls(void)
{
    fx_setup();
    fx_send("BOGUS");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 1);
    fx_send("*ESE 32");
    fx_send("*CLS");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
    fx_clear();
    fx_send("*ESR?;*STB?;*ESE?");
    CHECK_STR(fx_out(), "0;0;32\n");     /* ESR cleared, ESE preserved */
}

static void test_opc(void)
{
    fx_setup();
    fx_send("*OPC?");
    CHECK_STR(fx_out(), "1\n");
    fx_clear();
    fx_send("*OPC");
    CHECK_STR(fx_out(), "");
    fx_send("*ESR?");
    CHECK_STR(fx_out(), "1\n");          /* OPC bit set by *OPC */
    fx_clear();
    fx_send("*ESR?");
    CHECK_STR(fx_out(), "0\n");          /* reading clears it */
}

static void test_wai(void)
{
    fx_setup();
    fx_send("*WAI");
    CHECK_STR(fx_out(), "");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_tst(void)
{
    fx_setup();
    fx_send("*TST?");
    CHECK_STR(fx_out(), "0\n");
}

static void test_esr_reflects_error_class(void)
{
    fx_setup();
    fx_send("BOGUS");                  /* -113: command error, bit 5 */
    fx_send("*ESR?");
    CHECK_STR(fx_out(), "32\n");
    fx_clear();

    fx_send("FAIL");                   /* -200: execution error, bit 4 */
    fx_send("*ESR?");
    CHECK_STR(fx_out(), "16\n");
    fx_clear();

    viper_scpi_error_push(&g_ctx, -300);   /* device dependent, bit 3 */
    viper_scpi_error_push(&g_ctx, -410);   /* query error, bit 2 */
    viper_scpi_error_push(&g_ctx, 42);     /* positive: device specific, bit 3 */
    fx_send("*ESR?");
    CHECK_STR(fx_out(), "12\n");
    fx_clear();

    viper_scpi_error_push(&g_ctx, -500);   /* no ESR bit */
    fx_send("*ESR?");
    CHECK_STR(fx_out(), "0\n");
}

static void test_status_byte(void)
{
    fx_setup();
    fx_send("*STB?");
    CHECK_STR(fx_out(), "0\n");
    fx_clear();

    fx_send("BOGUS");                  /* error queue not empty: EAV (4) */
    fx_send("*STB?");
    CHECK_STR(fx_out(), "4\n");
    fx_clear();

    fx_send("*ESE 32");                /* CME enabled: ESB (32) */
    fx_send("*STB?");
    CHECK_STR(fx_out(), "36\n");
    fx_clear();

    fx_send("*SRE 32");                /* ESB enabled: MSS (64) */
    fx_send("*STB?");
    CHECK_STR(fx_out(), "100\n");
    CHECK_INT(viper_scpi_status_byte(&g_ctx), 100);
    fx_clear();

    fx_send("SYST:ERR?");              /* queue emptied: EAV clears */
    fx_clear();
    fx_send("*STB?");
    CHECK_STR(fx_out(), "96\n");       /* EAV cleared; ESB and MSS remain */
    fx_clear();
    fx_send("*ESR?");
    fx_clear();
    fx_send("*STB?");
    CHECK_STR(fx_out(), "0\n");
}

static void test_ese_sre(void)
{
    fx_setup();
    fx_send("*ESE 255");
    fx_send("*ESE?");
    CHECK_STR(fx_out(), "255\n");
    fx_clear();
    fx_send("*ESE 0x0F");
    fx_send("*ESE?");
    CHECK_STR(fx_out(), "15\n");
    fx_clear();

    fx_send("*SRE 255");
    fx_send("*SRE?");
    CHECK_STR(fx_out(), "191\n");        /* bit 6 is never stored */
    fx_clear();
    fx_send("*SRE 16");
    fx_send("*SRE?");
    CHECK_STR(fx_out(), "16\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
    fx_clear();

    fx_send("*ESE 256");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -222);
    fx_send("*ESE -1");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -222);
    fx_send("*ESE");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -109);
    fx_send("*ESE abc");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -104);
    fx_send("*SRE 256");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -222);
    fx_send("*ESE? 1");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -108);
    fx_send("*ESE?");
    CHECK_STR(fx_out(), "15\n");         /* failed writes changed nothing */
}

static void test_malformed_common_commands(void)
{
    fx_setup();
    fx_send("*IDN");                   /* query-only command sent as a set */
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("*RST?");                  /* command-only sent as a query */
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("*FOO?");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("*");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("*IDNX?");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("*ID?");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("*IDN:X?");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("*RST 1");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -108);
    fx_send("*IDN? 1");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -108);
    fx_send("*OPC 1");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -108);
    CHECK_INT(g_reset_calls, 0);
    CHECK_STR(fx_out(), "");
}

static void test_combined_line(void)
{
    fx_setup();
    fx_send("*CLS;*IDN?;*OPC?;*STB?");
    CHECK_STR(fx_out(), "Acme,Widget,SN123,1.2.3;1;0\n");
}

static void test_event_set(void)
{
    fx_setup();
    viper_scpi_event_set(&g_ctx, VIPER_SCPI_ESR_URQ);
    fx_send("*ESR?");
    CHECK_STR(fx_out(), "64\n");
}

static void test_power_on(void)
{
    viper_scpi_context_t c;

    /* A fresh context reports the power-on event once (488.2 11.5.1.1.2). */
    fx_setup();
    CHECK_INT(viper_scpi_init(&c, g_ctx.cfg), 0);
    viper_scpi_input_buffer(&c, "*ESR?\n*ESR?\n", 12);
    CHECK_STR(fx_out(), "128\n0\n");
}

static void test_trg(void)
{
    /* The fixture has no trigger hook: *TRG does not exist. */
    fx_setup();
    fx_send("*TRG");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("*TRG?");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
}

/* *ESE and *SRE take decimal numeric data rounded to an integer (IEEE 488.2
 * 10.10, 10.34) and exactly one value. The STATus registers likewise. */
static void test_register_values(void)
{
    fx_setup();
    fx_send("*ESE 32.0;*SRE 3.2E1;*ESE?;*SRE?");
    CHECK_STR(fx_out(), "32;32\n");
    fx_clear();
    fx_send("*ESE 31.6;*ESE?");
    CHECK_STR(fx_out(), "32\n");
    fx_clear();
    fx_send("*ESE 1,2");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -108);
    fx_send("*SRE 16,99");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -108);
    fx_send("*ESE?;*SRE?");
    CHECK_STR(fx_out(), "32;32\n");                 /* unchanged */
    fx_clear();
    fx_send("*ESE 255.5");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -222);
    fx_send("STAT:OPER:ENAB 16.0;ENAB?");
    CHECK_STR(fx_out(), "16\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static int s_saved = -1;
static int s_clears;

static viper_scpi_result_t cb_sav(viper_scpi_context_t *ctx)
{
    int32_t slot;
    viper_scpi_result_t res = viper_scpi_expect(ctx, 1, VIPER_SCPI_NO_FORM);
    if (res == VIPER_SCPI_RES_OK) {
        static const viper_scpi_integer_t slots = { .min = 0, .max = 9 };
        res = viper_scpi_param_integer(ctx, 0, &slots, &slot, NULL);
    }
    if (res == VIPER_SCPI_RES_OK) {
        s_saved = (int)slot;
    }
    return res;
}

static viper_scpi_result_t cb_opt(viper_scpi_context_t *ctx)
{
    viper_scpi_result_text(ctx, "GPIB,DIGIO");
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cb_my_idn(viper_scpi_context_t *ctx)
{
    viper_scpi_result_text(ctx, "MINE");
    return VIPER_SCPI_RES_OK;
}

static int32_t self_test_fails(viper_scpi_context_t *ctx)
{
    (void)ctx;
    return 3;
}

static void on_clear(viper_scpi_context_t *ctx)
{
    (void)ctx;
    s_clears++;
}

/* The instrument's own common commands and the *CLS / *TST? hooks. */
static void test_user_common_commands(void)
{
    static const viper_scpi_node_t common[] = {
        { "SAV", cb_sav, NULL },
        { "OPT", cb_opt, NULL },
        { "IDN", cb_my_idn, NULL },     /* replaces the built-in */
        VIPER_SCPI_NODE_END
    };
    viper_scpi_config_t cfg = *(g_ctx.cfg);

    cfg.common = common;
    cfg.clear = on_clear;
    cfg.self_test = self_test_fails;
    fx_setup();
    (void)viper_scpi_init(&g_ctx, &cfg);
    fx_clear();
    fx_send("*SAV 4;*opt?;*IDN?;*TST?;*CLS;*ESR?");
    CHECK_INT(s_saved, 4);
    CHECK_STR(fx_out(), "GPIB,DIGIO;MINE;3;0\n");
    CHECK_INT(s_clears, 1);
    fx_send("*SAV 10");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -222);
    fx_send("*RCL 1");                    /* neither table has it */
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_setup();                           /* restore the fixture */
}

int main(void)
{
    test_idn();
    test_idn_defaults();
    test_rst();
    test_cls();
    test_opc();
    test_wai();
    test_tst();
    test_esr_reflects_error_class();
    test_status_byte();
    test_ese_sre();
    test_malformed_common_commands();
    test_combined_line();
    test_event_set();
    test_power_on();
    test_trg();
    test_register_values();
    test_user_common_commands();
    return test_summary("test_ieee488");
}
