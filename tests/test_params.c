/* Parameter extraction and response formatting. */
#include "test_util.h"

static int err(void)
{
    return viper_scpi_error_pop(&g_ctx);
}

static void test_integers(void)
{
    fx_setup();
    fx_send("COUNT 123");
    CHECK_INT(g_count, 123);
    fx_send("COUNT -45");
    CHECK_INT(g_count, -45);
    fx_send("COUNT +7");
    CHECK_INT(g_count, 7);
    fx_send("COUNT 0x1F");
    CHECK_INT(g_count, 31);
    fx_send("COUNT 0Xff");
    CHECK_INT(g_count, 255);
    fx_send("COUNT #HAB");
    CHECK_INT(g_count, 171);
    fx_send("COUNT #B1011");
    CHECK_INT(g_count, 11);
    fx_send("COUNT #b101");
    CHECK_INT(g_count, 5);
    fx_send("COUNT #Q17");
    CHECK_INT(g_count, 15);
    fx_send("COUNT -0x10");
    CHECK_INT(g_count, -16);
    fx_send("COUNT 2147483647");
    CHECK_INT(g_count, 2147483647L);
    fx_send("COUNT -2147483648");
    CHECK_INT(g_count, -2147483647L - 1);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);

    fx_clear();
    fx_send("COUNT 0x7F;COUNT?");
    CHECK_STR(fx_out(), "127\n");
}

static void test_integer_errors(void)
{
    fx_setup();
    fx_send("COUNT 2147483648");
    CHECK_INT(err(), -222);
    fx_send("COUNT -2147483649");
    CHECK_INT(err(), -222);
    fx_send("COUNT 99999999999999999999999");
    CHECK_INT(err(), -222);
    fx_send("COUNT 0xFFFFFFFF");
    CHECK_INT(err(), -222);
    fx_send("COUNT abc");
    CHECK_INT(err(), -104);
    fx_send("COUNT 1.5");
    CHECK_INT(err(), -104);
    fx_send("COUNT 0x");
    CHECK_INT(err(), -104);
    fx_send("COUNT #B102");
    CHECK_INT(err(), -104);
    fx_send("COUNT #Q8");
    CHECK_INT(err(), -104);
    fx_send("COUNT #X1");
    CHECK_INT(err(), -104);
    fx_send("COUNT #B");
    CHECK_INT(err(), -104);
    fx_send("COUNT -");
    CHECK_INT(err(), -104);
    fx_send("COUNT 12ab");
    CHECK_INT(err(), -104);
    fx_send("COUNT");               /* missing parameter */
    CHECK_INT(err(), -109);
    CHECK_INT(g_count, 0);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_unsigned(void)
{
    fx_setup();
    fx_send("UCOUNT 0xFFFFFFFF");
    CHECK(g_ucount == 4294967295u);
    fx_clear();
    fx_send("UCOUNT?");
    CHECK_STR(fx_out(), "4294967295\n");
    fx_send("UCOUNT -0");
    CHECK(g_ucount == 0);
    fx_send("UCOUNT -1");
    CHECK_INT(err(), -222);
    fx_send("UCOUNT 4294967296");
    CHECK_INT(err(), -222);
    fx_send("UCOUNT x");
    CHECK_INT(err(), -104);
}

static void test_floats(void)
{
    fx_setup();
    fx_send("SOUR:VOLT 1.23E-3");
    CHECK(g_volt == 1.23e-3);
    fx_send("SOUR:VOLT -4.5e2");
    CHECK(g_volt == -450.0);
    fx_send("SOUR:VOLT +.5");
    CHECK(g_volt == 0.5);
    fx_send("SOUR:VOLT 5.");
    CHECK(g_volt == 5.0);
    fx_send("SOUR:VOLT 10");
    CHECK(g_volt == 10.0);
    fx_send("SOUR:VOLT 2E+3");
    CHECK(g_volt == 2000.0);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);

    fx_clear();
    fx_send("SOUR:VOLT 1.23E-3;VOLT?");
    CHECK_STR(fx_out(), "0.00123\n");
}

static void test_float_errors(void)
{
    fx_setup();
    fx_send("SOUR:VOLT abc");
    CHECK_INT(err(), -104);
    fx_send("SOUR:VOLT 1e");
    CHECK_INT(err(), -104);
    fx_send("SOUR:VOLT e5");
    CHECK_INT(err(), -104);
    fx_send("SOUR:VOLT .");
    CHECK_INT(err(), -104);
    fx_send("SOUR:VOLT 1.2.3");
    CHECK_INT(err(), -104);
    fx_send("SOUR:VOLT nanx");          /* INF/NINF/NAN are keywords (see */
    CHECK_INT(err(), -104);             /* test_special_values); near   */
    fx_send("SOUR:VOLT infinit");       /* misses are not               */
    CHECK_INT(err(), -104);
    fx_send("SOUR:VOLT 0x10");
    CHECK_INT(err(), -104);
    fx_send("SOUR:VOLT 1e999");
    CHECK_INT(err(), -222);
    fx_send("SOUR:VOLT 1234567890123456789012345678901234567890123456789");
    CHECK_INT(err(), -104);            /* longer than the conversion buffer */
    fx_send("SOUR:VOLT");
    CHECK_INT(err(), -109);
}

static void test_double_results(void)
{
    fx_setup();
    g_volt = 5;
    fx_send("SOUR:VOLT?");
    CHECK_STR(fx_out(), "5\n");
    fx_clear();
    g_volt = 1e-6;
    fx_send("SOUR:VOLT?");
    CHECK_STR(fx_out(), "1E-06\n");
    fx_clear();
    g_volt = 1.0 / 0.0;
    fx_send("SOUR:VOLT?");
    CHECK_STR(fx_out(), "9.9E+37\n");
    fx_clear();
    g_volt = -1.0 / 0.0;
    fx_send("SOUR:VOLT?");
    CHECK_STR(fx_out(), "-9.9E+37\n");
    fx_clear();
    g_volt = 0.0 / 0.0 * 1.0;
    g_volt = g_volt - g_volt;
    fx_send("SOUR:VOLT?");
    CHECK_STR(fx_out(), "9.91E+37\n");
}

/* Formats @p v with result_double_digits() in a fresh context. */
static const char *fmt(double v, int digits)
{
    fx_setup();
    if (digits == 0) {
        viper_scpi_result_double(&g_ctx, v);
    } else {
        viper_scpi_result_double_digits(&g_ctx, v, digits);
    }
    return fx_out();
}

static void test_nr3_format(void)
{
    /* Upper-case exponent (IEEE 488.2 8.7.4) and 15 significant digits. */
    CHECK_STR(fmt(1e10, 0), "10000000000");
    CHECK_STR(fmt(1e20, 0), "1E+20");
    CHECK_STR(fmt(1.5e-7, 0), "1.5E-07");
    CHECK_STR(fmt(-1.5e-7, 0), "-1.5E-07");
    CHECK_STR(fmt(123456789012.0, 0), "123456789012");
    CHECK_STR(fmt(1.23456789012345e30, 0), "1.23456789012345E+30");
    CHECK_STR(fmt(0.0, 0), "0");
    CHECK_STR(fmt(-0.25, 0), "-0.25");
    CHECK_STR(fmt(0.1, 0), "0.1");                 /* no noise digits */
    CHECK_STR(fmt(1.23456789012, 0), "1.23456789012");
    CHECK_STR(fmt(-42.0, 0), "-42");

    /* Explicit digit counts, clamped to 1..17. */
    CHECK_STR(fmt(1.23456789, 3), "1.23");
    CHECK_STR(fmt(123456.789, 7), "123456.8");
    CHECK_STR(fmt(1.5e-7, 2), "1.5E-07");
    CHECK_STR(fmt(0.1, 17), "0.10000000000000001");
    CHECK_STR(fmt(0.1, 99), "0.10000000000000001");
    CHECK_STR(fmt(2.6, -3), "3");                  /* clamped to 1 digit */
    CHECK_STR(fmt(1e10, 3), "1E+10");

    /* Special values keep the SCPI encoding in every precision. */
    CHECK_STR(fmt(1.0 / 0.0, 3), "9.9E+37");
    CHECK_STR(fmt(-1.0 / 0.0, 0), "-9.9E+37");

    /* A value set with 12 digits reads back unchanged. */
    fx_setup();
    fx_send("SOUR:VOLT 1.23456789012;VOLT?");
    CHECK_STR(fx_out(), "1.23456789012\n");
}

static void test_special_values(void)
{
    double d = 0;
    int32_t i = 0;
    viper_scpi_numeric_kw_t kw = VIPER_SCPI_KW_MIN;

    fx_setup();
    fx_send("PARAMS INF,infinity,NINF,NINFINITY,NAN,nan");
    CHECK_INT(viper_scpi_param_double(&g_ctx, 0, &d), 0);
    CHECK(d > 1.7976931348623157e308);
    CHECK_INT(viper_scpi_param_double(&g_ctx, 1, &d), 0);
    CHECK(d > 1.7976931348623157e308);
    CHECK_INT(viper_scpi_param_double(&g_ctx, 2, &d), 0);
    CHECK(d < -1.7976931348623157e308);
    CHECK_INT(viper_scpi_param_double(&g_ctx, 3, &d), 0);
    CHECK(d < -1.7976931348623157e308);
    CHECK_INT(viper_scpi_param_double(&g_ctx, 4, &d), 0);
    CHECK(d != d);
    CHECK_INT(viper_scpi_param_double(&g_ctx, 5, &d), 0);
    CHECK(d != d);
    CHECK_INT(viper_scpi_param_double_unit(&g_ctx, 0, viper_scpi_units_volt, &d), 0);
    CHECK(d > 1.7976931348623157e308);
    CHECK_INT(viper_scpi_param_int32(&g_ctx, 0, &i), VIPER_SCPI_RES_ERR_DATA_TYPE);

    /* Range-checked parsers: INF must fit [min, max]; NAN never does. */
    CHECK_INT(viper_scpi_param_double_kw(&g_ctx, 0, 0.0, 10.0, 1.0, &d),
              VIPER_SCPI_RES_ERR_OUT_OF_RANGE);
    CHECK_INT(viper_scpi_param_double_kw(&g_ctx, 2, -10.0, 10.0, 1.0, &d),
              VIPER_SCPI_RES_ERR_OUT_OF_RANGE);
    CHECK_INT(viper_scpi_param_double_kw(&g_ctx, 4, -10.0, 10.0, 1.0, &d),
              VIPER_SCPI_RES_ERR_ILLEGAL_VALUE);
    CHECK_INT(viper_scpi_param_double_unit_kw(&g_ctx, 0, viper_scpi_units_volt, 0.0, 10.0,
                                              1.0, &d), VIPER_SCPI_RES_ERR_OUT_OF_RANGE);
    CHECK_INT(viper_scpi_param_double_kw(&g_ctx, 0, 0.0, 1.0 / 0.0, 1.0, &d), 0);
    CHECK(d > 1.7976931348623157e308);         /* allowed when max is INF */
    CHECK_INT(viper_scpi_param_double_kw(&g_ctx, 0, 0.0, 10.0, 1.0, NULL),
              VIPER_SCPI_RES_ERR_PARAM_INVALID);

    /* Via a command: the response encodes INF as 9.9E+37. */
    fx_clear();
    fx_send("SOUR:VOLT INF;VOLT?");
    CHECK_STR(fx_out(), "9.9E+37\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);

    /* UP / DOWN only through the _ex parsers, which report the keyword. */
    fx_setup();
    fx_send("PARAMS UP,down,MIN,MAXIMUM,def,4.5,UPX");
    d = 7.0;
    CHECK_INT(viper_scpi_param_double_kw_ex(&g_ctx, 0, 0.0, 10.0, 1.0, &d, &kw), 0);
    CHECK_INT(kw, VIPER_SCPI_KW_UP);
    CHECK(d == 7.0);                           /* untouched: instrument steps */
    CHECK_INT(viper_scpi_param_double_kw_ex(&g_ctx, 1, 0.0, 10.0, 1.0, &d, &kw), 0);
    CHECK_INT(kw, VIPER_SCPI_KW_DOWN);
    CHECK_INT(viper_scpi_param_double_kw_ex(&g_ctx, 2, 0.0, 10.0, 1.0, &d, &kw), 0);
    CHECK_INT(kw, VIPER_SCPI_KW_MIN);
    CHECK(d == 0.0);
    CHECK_INT(viper_scpi_param_double_kw_ex(&g_ctx, 3, 0.0, 10.0, 1.0, &d, &kw), 0);
    CHECK_INT(kw, VIPER_SCPI_KW_MAX);
    CHECK(d == 10.0);
    CHECK_INT(viper_scpi_param_double_kw_ex(&g_ctx, 4, 0.0, 10.0, 1.0, &d, &kw), 0);
    CHECK_INT(kw, VIPER_SCPI_KW_DEF);
    CHECK(d == 1.0);
    CHECK_INT(viper_scpi_param_double_kw_ex(&g_ctx, 5, 0.0, 10.0, 1.0, &d, &kw), 0);
    CHECK_INT(kw, VIPER_SCPI_KW_NONE);
    CHECK(d == 4.5);
    CHECK_INT(viper_scpi_param_double_kw_ex(&g_ctx, 6, 0.0, 10.0, 1.0, &d, &kw),
              VIPER_SCPI_RES_ERR_DATA_TYPE);
    CHECK_INT(viper_scpi_param_double_kw_ex(&g_ctx, 0, 0.0, 10.0, 1.0, &d, NULL),
              VIPER_SCPI_RES_ERR_PARAM_INVALID);
    /* The plain _kw parsers keep rejecting UP/DOWN. */
    CHECK_INT(viper_scpi_param_double_kw(&g_ctx, 0, 0.0, 10.0, 1.0, &d),
              VIPER_SCPI_RES_ERR_DATA_TYPE);
    CHECK_INT(viper_scpi_param_int32_kw(&g_ctx, 1, 0, 10, 1, &i), VIPER_SCPI_RES_ERR_DATA_TYPE);

    i = 3;
    CHECK_INT(viper_scpi_param_int32_kw_ex(&g_ctx, 0, 0, 10, 1, &i, &kw), 0);
    CHECK_INT(kw, VIPER_SCPI_KW_UP);
    CHECK_INT(i, 3);
    CHECK_INT(viper_scpi_param_int32_kw_ex(&g_ctx, 3, 0, 10, 1, &i, &kw), 0);
    CHECK_INT(kw, VIPER_SCPI_KW_MAX);
    CHECK_INT(i, 10);
    CHECK_INT(viper_scpi_param_int32_kw_ex(&g_ctx, 5, 0, 10, 1, &i, &kw),
              VIPER_SCPI_RES_ERR_DATA_TYPE);       /* 4.5 is not an integer */
    CHECK_INT(viper_scpi_param_int32_kw_ex(&g_ctx, 0, 0, 10, 1, &i, NULL),
              VIPER_SCPI_RES_ERR_PARAM_INVALID);

    CHECK_INT(viper_scpi_param_double_unit_kw_ex(&g_ctx, 1, viper_scpi_units_volt, 0.0, 10.0,
                                                 1.0, &d, &kw), 0);
    CHECK_INT(kw, VIPER_SCPI_KW_DOWN);
    CHECK_INT(viper_scpi_param_double_unit_kw_ex(&g_ctx, 1, NULL, 0.0, 10.0, 1.0, &d, &kw),
              VIPER_SCPI_RES_ERR_PARAM_INVALID);
    fx_send("PARAMS 500MV");
    CHECK_INT(viper_scpi_param_double_unit_kw_ex(&g_ctx, 0, viper_scpi_units_volt, 0.0, 10.0,
                                                 1.0, &d, &kw), 0);
    CHECK_INT(kw, VIPER_SCPI_KW_NONE);
    CHECK(d == 0.5);
}

static void test_booleans(void)
{
    fx_setup();
    fx_send("OUTP:STAT ON");
    CHECK_INT(g_output, 1);
    fx_send("OUTP:STAT off");
    CHECK_INT(g_output, 0);
    fx_send("OUTP:STAT 1");
    CHECK_INT(g_output, 1);
    fx_send("OUTP:STAT 0");
    CHECK_INT(g_output, 0);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);

    fx_clear();
    fx_send("OUTP:STAT ON;STAT?");
    CHECK_STR(fx_out(), "1\n");

    fx_send("OUTP:STAT MAYBE");
    CHECK_INT(err(), -104);
    fx_send("OUTP:STAT 2");
    CHECK_INT(err(), -104);
    fx_send("OUTP:STAT O");
    CHECK_INT(err(), -104);
    fx_send("OUTP:STAT");
    CHECK_INT(err(), -109);
    CHECK_INT(g_output, 1);
}

static void test_choices(void)
{
    fx_setup();
    fx_send("DISP:MODE FAST");
    CHECK_INT(g_mode, 1);
    fx_send("DISP:MODE slow");
    CHECK_INT(g_mode, 2);
    fx_send("DISP:MODE NORM");
    CHECK_INT(g_mode, 0);
    fx_send("DISP:MODE NORMAL");
    CHECK_INT(g_mode, 0);
    fx_send("DISP:MODE FAST;MODE?");
    CHECK_STR(fx_out(), "1\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);

    fx_send("DISP:MODE TURBO");
    CHECK_INT(err(), -224);
    fx_send("DISP:MODE NOR");
    CHECK_INT(err(), -224);
    fx_send("DISP:MODE");
    CHECK_INT(err(), -109);
    CHECK_INT(g_mode, 1);
}

static void test_strings(void)
{
    fx_setup();
    fx_send("NAME \"hello\"");
    CHECK_STR(g_name, "hello");
    fx_send("NAME 'single'");
    CHECK_STR(g_name, "single");
    fx_send("NAME \"say \"\"hi\"\"\"");
    CHECK_STR(g_name, "say \"hi\"");
    fx_send("NAME 'it''s'");
    CHECK_STR(g_name, "it's");
    fx_send("NAME \"it's\"");
    CHECK_STR(g_name, "it's");
    fx_send("NAME \"\"");
    CHECK_STR(g_name, "");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);

    fx_send("NAME \"say \"\"hi\"\"\";:NAME?");
    CHECK_STR(fx_out(), "\"say \"\"hi\"\"\"\n");
}

static void test_string_errors(void)
{
    fx_setup();
    fx_send("NAME unquoted");
    CHECK_INT(err(), -104);
    fx_send("NAME \"abc\"x");
    CHECK_INT(err(), -104);
    fx_send("NAME \"a\"b\"c\"");
    CHECK_INT(err(), -104);
    fx_send("NAME \"a\"b\"");
    CHECK_INT(err(), -102);
    fx_send("NAME \"abc\"\"");     /* unterminated: last quote is an escape */
    CHECK_INT(err(), -102);
    fx_send("NAME \"0123456789012345678901234567890\""); /* 31 chars: fits */
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
    fx_send("NAME \"01234567890123456789012345678901\""); /* 32 chars: needs 33 */
    CHECK_INT(err(), -222);
    fx_send("NAME");
    CHECK_INT(err(), -109);
}

/* Direct calls to the extraction API from inside a callback context. */
static void test_api_argument_checks(void)
{
    const char *s = NULL;
    size_t n = 0;
    int32_t i32;
    uint32_t u32;
    double d;
    bool b;
    int c;
    char buf[8];
    static const viper_scpi_choice_t ch[] = { { "A", 1 }, { NULL, 0 } };

    fx_setup();
    fx_send("PARAMS 5,\"x\"");   /* parse only; the tokens stay in the context */
    /* After execution the params still describe the last unit. */
    CHECK_INT(viper_scpi_param_count(&g_ctx), 2);
    CHECK(viper_scpi_param_present(&g_ctx, 1));
    CHECK(!viper_scpi_param_present(&g_ctx, 2));
    CHECK_INT(viper_scpi_param_raw(&g_ctx, 0, &s, &n), 0);
    CHECK_INT(n, 1);
    CHECK(s[0] == '5');
    CHECK_INT(viper_scpi_param_raw(&g_ctx, 2, &s, &n), VIPER_SCPI_RES_ERR_MISSING_PARAM);
    CHECK_INT(viper_scpi_param_raw(&g_ctx, 0, NULL, &n), VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(viper_scpi_param_int32(&g_ctx, 0, NULL), VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(viper_scpi_param_int32(&g_ctx, 0, &i32), 0);
    CHECK_INT(i32, 5);
    CHECK_INT(viper_scpi_param_uint32(&g_ctx, 0, NULL), VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(viper_scpi_param_uint32(&g_ctx, 0, &u32), 0);
    CHECK_INT(viper_scpi_param_double(&g_ctx, 0, NULL), VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(viper_scpi_param_double(&g_ctx, 0, &d), 0);
    CHECK(d == 5.0);
    CHECK_INT(viper_scpi_param_bool(&g_ctx, 0, NULL), VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(viper_scpi_param_bool(&g_ctx, 0, &b), VIPER_SCPI_RES_ERR_DATA_TYPE);
    CHECK_INT(viper_scpi_param_choice(&g_ctx, 0, NULL, &c), VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(viper_scpi_param_choice(&g_ctx, 0, ch, NULL), VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(viper_scpi_param_choice(&g_ctx, 0, ch, &c), VIPER_SCPI_RES_ERR_ILLEGAL_VALUE);
    CHECK_INT(viper_scpi_param_string(&g_ctx, 1, NULL, 8, NULL), VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(viper_scpi_param_string(&g_ctx, 1, buf, 0, NULL), VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(viper_scpi_param_string(&g_ctx, 1, buf, sizeof buf, &n), 0);
    CHECK_INT(n, 1);
    CHECK_STR(buf, "x");
    CHECK_INT(viper_scpi_param_string(&g_ctx, 1, buf, 1, NULL), VIPER_SCPI_RES_ERR_OUT_OF_RANGE);
    CHECK_INT(viper_scpi_param_string(&g_ctx, 5, buf, sizeof buf, NULL), VIPER_SCPI_RES_ERR_MISSING_PARAM);
}

int main(void)
{
    test_integers();
    test_integer_errors();
    test_unsigned();
    test_floats();
    test_float_errors();
    test_double_results();
    test_nr3_format();
    test_special_values();
    test_booleans();
    test_choices();
    test_strings();
    test_string_errors();
    test_api_argument_checks();
    return test_summary("test_params");
}
