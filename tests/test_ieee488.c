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
    return test_summary("test_ieee488");
}
