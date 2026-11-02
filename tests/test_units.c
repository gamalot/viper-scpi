/* Numbers with SCPI units and multiplier prefixes. */
#include "test_util.h"

static int err(void)
{
    return viper_scpi_error_pop(&g_ctx);
}

/* Sends "<cmd> <value>" and returns the parsed number (g_num). */
static double set(const char *cmd, const char *value)
{
    char line[64];

    g_num = -12345.0;
    (void)snprintf(line, sizeof line, "%s %s", cmd, value);
    fx_send(line);
    return g_num;
}

static void test_hertz(void)
{
    fx_setup();
    CHECK_NEAR(set("FREQ", "1.5KHZ"), 1500.0);
    CHECK_NEAR(set("FREQ", "10MHZ"), 1e7);         /* MHZ is mega */
    CHECK_NEAR(set("FREQ", "5mhz"), 5e6);
    CHECK_NEAR(set("FREQ", "1GHZ"), 1e9);
    CHECK_NEAR(set("FREQ", "2THZ"), 2e12);
    CHECK_NEAR(set("FREQ", "1MAHZ"), 1e6);
    CHECK_NEAR(set("FREQ", "1EXHZ"), 1e18);
    CHECK_NEAR(set("FREQ", "1PEHZ"), 1e15);
    CHECK_NEAR(set("FREQ", "3UHZ"), 3e-6);
    CHECK_NEAR(set("FREQ", "4NHZ"), 4e-9);
    CHECK_NEAR(set("FREQ", "5PHZ"), 5e-12);
    CHECK_NEAR(set("FREQ", "6FHZ"), 6e-15);
    CHECK_NEAR(set("FREQ", "7AHZ"), 7e-18);
    CHECK_NEAR(set("FREQ", "100HZ"), 100.0);
    CHECK_NEAR(set("FREQ", "100 hz"), 100.0);
    CHECK_NEAR(set("FREQ", "-2KHZ"), -2000.0);
    CHECK_NEAR(set("FREQ", "+2KHZ"), 2000.0);
    CHECK_NEAR(set("FREQ", "1.KHZ"), 1000.0);
    CHECK_NEAR(set("FREQ", ".5KHZ"), 500.0);
    CHECK_NEAR(set("FREQ", "2.5E3HZ"), 2500.0);
    CHECK_NEAR(set("FREQ", "1E+3 KHZ"), 1e6);
    CHECK_NEAR(set("FREQ", "1e-3MHZ"), 1e3);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_bare_numbers_allowed(void)
{
    fx_setup();
    CHECK_NEAR(set("FREQ", "100"), 100.0);
    CHECK_NEAR(set("FREQ", "1E3"), 1000.0);       /* exponent, not a unit */
    CHECK_NEAR(set("FREQ", "-1.5E-2"), -0.015);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
    fx_clear();
    fx_send("FREQ 2KHZ;FREQ?");
    CHECK_STR(fx_out(), "2000\n");
}

static void test_ohm_amp_farad(void)
{
    fx_setup();
    CHECK_NEAR(set("RES", "1MOHM"), 1e6);          /* MOHM is mega */
    CHECK_NEAR(set("RES", "5 mohm"), 5e6);
    CHECK_NEAR(set("RES", "1KOHM"), 1e3);
    CHECK_NEAR(set("RES", "10OHM"), 10.0);
    CHECK_NEAR(set("RES", "1MAOHM"), 1e6);
    CHECK_NEAR(set("AMPS", "10MA"), 0.01);         /* MA on amps: milliamp */
    CHECK_NEAR(set("AMPS", "1A"), 1.0);
    CHECK_NEAR(set("AMPS", "3 uA"), 3e-6);
    CHECK_NEAR(set("AMPS", "2PA"), 2e-12);
    CHECK_NEAR(set("AMPS", "1AA"), 1e-18);         /* atto-amp */
    CHECK_NEAR(set("AMPS", "1MAA"), 1e6);          /* mega-amp */
    CHECK_NEAR(set("CAP", "10PF"), 1e-11);
    CHECK_NEAR(set("CAP", "1FF"), 1e-15);          /* femto-farad */
    CHECK_NEAR(set("CAP", "1F"), 1.0);
    CHECK_NEAR(set("CAP", "2UF"), 2e-6);
    CHECK_NEAR(set("WATT", "3MW"), 3e-3);
    CHECK_NEAR(set("WATT", "2KW"), 2e3);
    CHECK_NEAR(set("IND", "4UH"), 4e-6);
    CHECK_NEAR(set("GAIN", "-3DB"), -3.0);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_time_and_celsius(void)
{
    fx_setup();
    CHECK_NEAR(set("TIM", "5MS"), 0.005);
    CHECK_NEAR(set("TIM", "2MIN"), 120.0);
    CHECK_NEAR(set("TIM", "1HOUR"), 3600.0);
    CHECK_NEAR(set("TIM", "1.5SEC"), 1.5);
    CHECK_NEAR(set("TIM", "10S"), 10.0);
    CHECK_NEAR(set("TIM", "1USEC"), 1e-6);
    CHECK_NEAR(set("TIM", "3NS"), 3e-9);
    CHECK_NEAR(set("CTMP", "20CEL"), 20.0);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);

    (void)set("TIM", "2MMIN");                     /* MIN takes no prefix */
    CHECK_INT(err(), -131);
    (void)set("TIM", "2KHOUR");
    CHECK_INT(err(), -131);
    (void)set("CTMP", "20KCEL");
    CHECK_INT(err(), -131);
}

static void test_suffix_errors(void)
{
    fx_setup();
    (void)set("FREQ", "1V");                       /* wrong unit family */
    CHECK_INT(err(), -131);
    (void)set("FREQ", "1KHZZ");
    CHECK_INT(err(), -131);
    (void)set("FREQ", "1XHZ");
    CHECK_INT(err(), -131);
    (void)set("FREQ", "1K");                       /* prefix without unit */
    CHECK_INT(err(), -131);
    (void)set("FREQ", "1E");
    CHECK_INT(err(), -131);
    (void)set("FREQ", "1.2.3HZ");
    CHECK_INT(err(), -131);
    (void)set("FREQ", "1 2");
    CHECK_INT(err(), -131);
    (void)set("FREQ", "1KHZ2");
    CHECK_INT(err(), -131);
    CHECK_NEAR(set("FREQ", "7"), 7.0);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
    CHECK_NEAR(g_num, 7.0);
}

static void test_number_errors(void)
{
    fx_setup();
    (void)set("FREQ", "KHZ");                      /* no number at all */
    CHECK_INT(err(), -104);
    (void)set("FREQ", "abc");
    CHECK_INT(err(), -104);
    (void)set("FREQ", "-");
    CHECK_INT(err(), -104);
    (void)set("FREQ", ".");
    CHECK_INT(err(), -104);
    (void)set("FREQ", "1e999HZ");
    CHECK_INT(err(), -222);
    (void)set("FREQ", "1E300EXHZ");                /* overflows after scaling */
    CHECK_INT(err(), -222);
    (void)set("FREQ", "1234567890123456789012345678901234567890123456789HZ");
    CHECK_INT(err(), -104);
    fx_send("FREQ");
    CHECK_INT(err(), -109);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_plain_double_rejects_units(void)
{
    fx_setup();
    fx_send("SOUR:VOLT 1.5V");     /* callback uses the plain parser */
    CHECK_INT(err(), -104);
}

static void test_unit_with_keywords(void)
{
    fx_setup();
    fx_send("VLIM 500MV");
    CHECK_NEAR(g_vlim, 0.5);
    fx_send("VLIM MAX");
    CHECK_NEAR(g_vlim, 10.0);
    fx_send("VLIM min");
    CHECK_NEAR(g_vlim, -10.0);
    fx_send("VLIM DEF");
    CHECK_NEAR(g_vlim, 1.0);
    fx_send("VLIM -10V");
    CHECK_NEAR(g_vlim, -10.0);
    fx_send("VLIM 5");
    CHECK_NEAR(g_vlim, 5.0);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);

    fx_send("VLIM 11V");
    CHECK_INT(err(), -222);
    fx_send("VLIM 20000MV");                        /* range checked after scaling */
    CHECK_INT(err(), -222);
    fx_send("VLIM 1KHZ");
    CHECK_INT(err(), -131);
    fx_send("VLIM");
    CHECK_INT(err(), -109);
    CHECK_NEAR(g_vlim, 5.0);
}

static void test_api_arguments(void)
{
    double d = 0;

    fx_setup();
    fx_send("PARAMS 1KHZ");
    CHECK_INT(viper_scpi_param_double_unit(&g_ctx, 0, NULL, &d), VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(viper_scpi_param_double_unit(&g_ctx, 0, viper_scpi_units_hertz, NULL),
              VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(viper_scpi_param_double_unit(&g_ctx, 5, viper_scpi_units_hertz, &d),
              VIPER_SCPI_RES_ERR_MISSING_PARAM);
    CHECK_INT(viper_scpi_param_double_unit(&g_ctx, 0, viper_scpi_units_hertz, &d), 0);
    CHECK_NEAR(d, 1000.0);
    CHECK_INT(viper_scpi_param_double_unit_kw(&g_ctx, 0, viper_scpi_units_hertz, 5.0, 1.0, 2.0, &d),
              VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(viper_scpi_param_double_unit_kw(&g_ctx, 0, viper_scpi_units_hertz, 0.0, 1.0, 0.5, NULL),
              VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(viper_scpi_param_double_unit_kw(&g_ctx, 0, viper_scpi_units_hertz, 0.0, 1e6, 0.5, &d), 0);
    CHECK_NEAR(d, 1000.0);
    CHECK_INT(viper_scpi_param_double_unit_kw(&g_ctx, 0, viper_scpi_units_hertz, 0.0, 10.0, 0.5, &d),
              VIPER_SCPI_RES_ERR_OUT_OF_RANGE);
}

/* A user-defined unit table with scale factors and flags. */
static void test_custom_unit_table(void)
{
    static const viper_scpi_unit_t pressure[] = {
        { "PA", 1.0, 0 },
        { "BAR", 1e5, VIPER_SCPI_UNIT_NO_PREFIX },
        { "PSI", 6894.757, VIPER_SCPI_UNIT_NO_PREFIX },
        { NULL, 0, 0 }
    };
    double d = 0;

    fx_setup();
    fx_send("PARAMS 2BAR,1KPA,3 psi,4MPA");
    CHECK_INT(viper_scpi_param_double_unit(&g_ctx, 0, pressure, &d), 0);
    CHECK_NEAR(d, 2e5);
    CHECK_INT(viper_scpi_param_double_unit(&g_ctx, 1, pressure, &d), 0);
    CHECK_NEAR(d, 1000.0);
    CHECK_INT(viper_scpi_param_double_unit(&g_ctx, 2, pressure, &d), 0);
    CHECK_NEAR(d, 3 * 6894.757);
    CHECK_INT(viper_scpi_param_double_unit(&g_ctx, 3, pressure, &d), 0);
    CHECK_NEAR(d, 4e-3);                            /* M = milli by default */
}

/* A prefix scales by an exact power of ten: the result is the same double
 * as the number written with the exponent, so a limit given in base units
 * is not missed by one rounding step. */
static void test_prefix_scaling_is_exact(void)
{
    fx_setup();
    CHECK(set("TIM", "10US") == 10e-6);
    CHECK(set("TIM", "100NS") == 100e-9);
    CHECK(set("TIM", "5US") == 5e-6);
    CHECK(set("TIM", "250NS") == 250e-9);
    CHECK(set("AMPS", "9MA") == 9e-3);
    CHECK(set("AMPS", "0.3MA") == 0.3e-3);
    CHECK(set("FREQ", "2.5KHZ") == 2.5e3);
    CHECK(set("FREQ", "3MHZ") == 3e6);
    CHECK(set("TIM", "1.5MIN") == 90.0);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

int main(void)
{
    test_hertz();
    test_bare_numbers_allowed();
    test_ohm_amp_farad();
    test_time_and_celsius();
    test_suffix_errors();
    test_number_errors();
    test_plain_double_rejects_units();
    test_unit_with_keywords();
    test_api_arguments();
    test_custom_unit_table();
    test_prefix_scaling_is_exact();
    return test_summary("test_units");
}
