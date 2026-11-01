/* Parser state machine: matching, paths, separators, buffering, errors. */
#include "test_util.h"

static void test_init_validation(void)
{
    viper_scpi_context_t c;

    CHECK_INT(viper_scpi_init(NULL, NULL), VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(viper_scpi_init(&c, NULL), VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK(sizeof(viper_scpi_context_t) < 2048); /* static footprint budget */
    {
        char v[16];
        (void)snprintf(v, sizeof v, "%d.%d.%d", VIPER_SCPI_VERSION_MAJOR,
                       VIPER_SCPI_VERSION_MINOR, VIPER_SCPI_VERSION_PATCH);
        CHECK_STR(v, VIPER_SCPI_VERSION_STRING);
    }
}

static void test_long_and_short_forms(void)
{
    fx_setup();
    fx_send("MEASURE:VOLTAGE:DC?");
    CHECK_STR(fx_out(), "1.5\n");
    fx_clear();
    fx_send("MEAS:VOLT:DC?");
    CHECK_STR(fx_out(), "1.5\n");
    fx_clear();
    fx_send("measure:volt:dc?");
    CHECK_STR(fx_out(), "1.5\n");
    fx_clear();
    fx_send("MeAs:VoLtAgE:dC?");
    CHECK_STR(fx_out(), "1.5\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_partial_forms_rejected(void)
{
    fx_setup();
    fx_send("MEASU:VOLT:DC?");   /* neither short nor long form */
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("ME:VOLT:DC?");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("MEASUREX:VOLT:DC?");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    CHECK_STR(fx_out(), "");
}

static void test_pattern_match_direct(void)
{
    CHECK(viper_scpi_pattern_match("VOLTage", "VOLT", 4));
    CHECK(viper_scpi_pattern_match("VOLTage", "voltage", 7));
    CHECK(!viper_scpi_pattern_match("VOLTage", "VOL", 3));
    CHECK(!viper_scpi_pattern_match("VOLTage", "VOLTA", 5));
    CHECK(viper_scpi_pattern_match("DC", "dc", 2));
    CHECK(!viper_scpi_pattern_match("DC", "D", 1));
    CHECK(!viper_scpi_pattern_match("DC", "", 0));
}

static void test_leading_colon_and_whitespace(void)
{
    fx_setup();
    fx_send(":MEAS:VOLT:DC?");
    CHECK_STR(fx_out(), "1.5\n");
    fx_clear();
    fx_send("   \t MEAS:VOLT:DC?   ");
    CHECK_STR(fx_out(), "1.5\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_parameter_separator_whitespace(void)
{
    fx_setup();
    fx_send("SOUR:VOLT   \t 2.5  ");
    CHECK(g_volt == 2.5);
    fx_send("PARAMS 1 , 2 ,3");
    CHECK_STR(fx_out(), "3\n");
}

static void test_multiple_commands(void)
{
    fx_setup();
    fx_send("SOUR:VOLT 1.5;:SOUR:CURR 0.25;:SOUR:VOLT?;:SOUR:CURR?");
    CHECK_STR(fx_out(), "1.5;0.25\n");
    CHECK(g_volt == 1.5);
    CHECK(g_curr == 0.25);
}

static void test_relative_paths(void)
{
    fx_setup();
    /* CURR is resolved under SOURce because the previous command was SOUR:VOLT. */
    fx_send("SOUR:VOLT 3;CURR 4");
    CHECK(g_volt == 3.0);
    CHECK(g_curr == 4.0);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);

    /* A leading colon returns to the root, where CURR does not exist. */
    fx_send("SOUR:VOLT 3;:CURR 4");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);

    /* The path resets at the start of every new line. */
    fx_send("SOUR:VOLT 1");
    fx_send("CURR 2");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    CHECK(g_curr == 4.0);

    /* Deep relative path: sibling of the last node. */
    fx_clear();
    fx_send("MEAS:VOLT:DC?;PAIR?");
    CHECK_STR(fx_out(), "1.5;1,2\n");
}

static void test_common_commands_keep_path(void)
{
    fx_setup();
    fx_send("SOUR:VOLT 1;*CLS;CURR 2");
    CHECK(g_curr == 2.0);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_multiple_results_formatting(void)
{
    fx_setup();
    fx_send("MEAS:VOLT:PAIR?;:MEAS:VOLT:DC?;:COUNT?");
    CHECK_STR(fx_out(), "1,2;1.5;0\n");
}

static void test_terminators(void)
{
    fx_setup();
    viper_scpi_input_buffer(&g_ctx, "COUNT 7\r\nCOUNT?\r\n", 17);
    CHECK_STR(fx_out(), "7\n");
    fx_clear();

    fx_send("");
    fx_send("   ");
    CHECK_STR(fx_out(), "");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_byte_at_a_time(void)
{
    const char *msg = "COUNT 42;COUNT?\n";

    fx_setup();
    for (size_t i = 0; msg[i] != '\0'; i++) {
        viper_scpi_input_char(&g_ctx, msg[i]);
        if (msg[i] != '\n') {
            CHECK_STR(fx_out(), ""); /* nothing runs before the terminator */
        }
    }
    CHECK_STR(fx_out(), "42\n");
}

static void test_split_across_chunks(void)
{
    fx_setup();
    viper_scpi_input_buffer(&g_ctx, "SOUR:VO", 7);
    viper_scpi_input_buffer(&g_ctx, "LT 9;:SOUR:V", 12);
    viper_scpi_input_buffer(&g_ctx, "OLT?\n", 5);
    CHECK_STR(fx_out(), "9\n");
}

static void test_end_of_message(void)
{
    fx_setup();
    viper_scpi_end_of_message(&g_ctx); /* nothing buffered: no effect */
    CHECK_STR(fx_out(), "");
    viper_scpi_input_buffer(&g_ctx, "COUNT 5;COUNT?", 14);
    CHECK_STR(fx_out(), "");
    viper_scpi_end_of_message(&g_ctx);
    CHECK_STR(fx_out(), "5\n");
}

static void test_empty_units_ignored(void)
{
    fx_setup();
    fx_send(";;COUNT 1;;COUNT?;");
    CHECK_STR(fx_out(), "1\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_semicolon_and_comma_in_strings(void)
{
    fx_setup();
    fx_send("NAME \"a;b,c\";NAME?");
    CHECK_STR(g_name, "a;b,c");
    CHECK_STR(fx_out(), "\"a;b,c\"\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_undefined_and_syntax_errors(void)
{
    fx_setup();
    fx_send("BOGUS");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("SOUR:BOGUS?");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("MEAS?");              /* intermediate node has no callback */
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("SOUR::VOLT?");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -102);
    fx_send("SOUR:VOLT:?");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -102);
    fx_send("?");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -102);
    fx_send("SOUR!VOLT");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -102);
    fx_send("SOUR?VOLT");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -102);
    fx_send("PARAMS 1,,2");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -102);
    fx_send("PARAMS 1,");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -102);
    fx_send("NAME \"unterminated");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -102);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_error_does_not_stop_line(void)
{
    fx_setup();
    fx_send("BOGUS;COUNT 3;COUNT?");
    CHECK_STR(fx_out(), "3\n");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
}

static void test_callback_error_pushed(void)
{
    fx_setup();
    fx_send("FAIL");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), VIPER_SCPI_RES_ERR_EXECUTION);
}

static void test_too_many_parameters(void)
{
    fx_setup();
    fx_send("PARAMS 1,2,3,4,5,6,7,8");
    CHECK_STR(fx_out(), "8\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
    fx_clear();
    fx_send("PARAMS 1,2,3,4,5,6,7,8,9");
    CHECK_STR(fx_out(), "");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -108);
}

static void test_mnemonic_too_long(void)
{
    fx_setup();
    fx_send("A:B:C:D:E:F:G:H?");   /* 8 levels: allowed, just undefined */
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("A:B:C:D:E:F:G:H:I?"); /* 9 levels */
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -112);
}

static void test_buffer_overrun(void)
{
    char big[VIPER_SCPI_BUFFER_SIZE + 40];

    fx_setup();
    memset(big, 'A', sizeof big - 1);
    big[sizeof big - 1] = '\0';

    /* Exactly BUFFER_SIZE bytes still fits. */
    memcpy(big, "NAME ", 5);
    big[VIPER_SCPI_BUFFER_SIZE] = '\0';
    big[5] = '"';
    memset(big + 6, 'x', VIPER_SCPI_BUFFER_SIZE - 7);
    big[VIPER_SCPI_BUFFER_SIZE - 1] = '"';
    fx_send(big);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 1); /* name too long for the fixture, not overrun */
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -222);

    /* One byte more overruns; the whole line is discarded. */
    memset(big, 'A', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    fx_send(big);
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -363);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);

    /* The parser recovers on the next line. */
    fx_send("COUNT 1;COUNT?");
    CHECK_STR(fx_out(), "1\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_overrun_via_end_of_message(void)
{
    char big[VIPER_SCPI_BUFFER_SIZE + 10];

    fx_setup();
    memset(big, 'A', sizeof big);
    viper_scpi_input_buffer(&g_ctx, big, sizeof big);
    viper_scpi_end_of_message(&g_ctx);
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -363);
}

static void test_user_data_and_null_write(void)
{
    static int marker;
    static viper_scpi_config_t quiet;
    viper_scpi_context_t c;

    quiet.write = NULL;
    quiet.user_data = &marker;
    CHECK_INT(viper_scpi_init(&c, &quiet), VIPER_SCPI_RES_OK);
    CHECK(viper_scpi_user_data(&c) == &marker);
    viper_scpi_input_buffer(&c, "*IDN?\n", 6); /* must not crash without a writer */
    CHECK_INT(viper_scpi_error_count(&c), 0);
}

int main(void)
{
    test_init_validation();
    test_long_and_short_forms();
    test_partial_forms_rejected();
    test_pattern_match_direct();
    test_leading_colon_and_whitespace();
    test_parameter_separator_whitespace();
    test_multiple_commands();
    test_relative_paths();
    test_common_commands_keep_path();
    test_multiple_results_formatting();
    test_terminators();
    test_byte_at_a_time();
    test_split_across_chunks();
    test_end_of_message();
    test_empty_units_ignored();
    test_semicolon_and_comma_in_strings();
    test_undefined_and_syntax_errors();
    test_error_does_not_stop_line();
    test_callback_error_pushed();
    test_too_many_parameters();
    test_mnemonic_too_long();
    test_buffer_overrun();
    test_overrun_via_end_of_message();
    test_user_data_and_null_write();
    return test_summary("test_parser");
}
