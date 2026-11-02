/* Optional nodes, numeric suffixes, block data and MIN/MAX/DEF keywords. */
#include "test_util.h"

static int err(void)
{
    return viper_scpi_error_pop(&g_ctx);
}

/* ---- Optional nodes ---------------------------------------------------- */

static void test_optional_leaf(void)
{
    fx_setup();
    fx_send("POW?");
    CHECK_STR(fx_out(), "POWER\n");
    fx_clear();
    fx_send("POWER:LEVEL?");
    CHECK_STR(fx_out(), "POWER\n");
    fx_clear();
    fx_send("pow:lev?");
    CHECK_STR(fx_out(), "POWER\n");
    fx_clear();
    fx_send("POW:PROT?");
    CHECK_STR(fx_out(), "PROT\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_optional_intermediate(void)
{
    fx_setup();
    fx_send("TEMP?");
    CHECK_STR(fx_out(), "25\n");
    fx_clear();
    fx_send("SENS:TEMP?");
    CHECK_STR(fx_out(), "25\n");
    fx_clear();
    fx_send(":SENSE:TEMPERATURE?");
    CHECK_STR(fx_out(), "25\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_optional_errors(void)
{
    fx_setup();
    fx_send("POW:LEV:X?");
    CHECK_INT(err(), -113);
    fx_send("SENS?");            /* SENSe has no default leaf */
    CHECK_INT(err(), -113);
    fx_send("LEV?");             /* LEVel lives below POWer, not at the root */
    CHECK_INT(err(), -113);
    CHECK_STR(fx_out(), "");
}

static void test_optional_relative_path(void)
{
    fx_setup();
    /* After POW:PROT the path is POW:, so LEV is reachable. */
    fx_send("POW:PROT?;LEV?");
    CHECK_STR(fx_out(), "PROT;POWER\n");
    fx_clear();
    /* After a bare POW? the path stays at the root. */
    fx_send("POW?;PROT?");
    CHECK_INT(err(), -113);
    CHECK_STR(fx_out(), "POWER\n");
}

/* ---- Numeric suffixes ---------------------------------------------------- */

static void test_suffix_values(void)
{
    fx_setup();
    fx_send("CHAN:VOLT?");
    CHECK_STR(fx_out(), "1\n");             /* omitted: default */
    fx_clear();
    fx_send("CHAN1:VOLT?");
    CHECK_STR(fx_out(), "1\n");
    fx_clear();
    fx_send("CHAN2:VOLT?");
    CHECK_STR(fx_out(), "2\n");
    fx_clear();
    fx_send("channel12:volt?");
    CHECK_STR(fx_out(), "12\n");
    fx_clear();
    fx_send("CHAN0:VOLT?");
    CHECK_STR(fx_out(), "0\n");
    fx_clear();
    fx_send("CHAN999999999:VOLT?");
    CHECK_STR(fx_out(), "999999999\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_suffix_multiple(void)
{
    fx_setup();
    fx_send("CHAN2:RANG3?");
    CHECK_STR(fx_out(), "2,3\n");
    fx_clear();
    fx_send("CHAN2:RANG?");
    CHECK_STR(fx_out(), "2,1\n");
    fx_clear();
    fx_send("CHAN:RANG4?");
    CHECK_STR(fx_out(), "1,4\n");
    fx_clear();
    fx_send("CHAN5:VOLT?;RANG6?");
    CHECK_STR(fx_out(), "5;5,6\n");   /* the inherited prefix counts */
}

static void test_suffix_optional_node(void)
{
    fx_setup();
    fx_send("CHAN2:BANK3:ID?");
    CHECK_STR(fx_out(), "2,3,-7\n");
    fx_clear();
    fx_send("CHAN2:ID?");                 /* BANK skipped: its slot is "omitted" */
    CHECK_STR(fx_out(), "2,1,-7\n");
    fx_clear();
    fx_send("CHAN:BANK:ID?");
    CHECK_STR(fx_out(), "1,1,-7\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_suffix_errors(void)
{
    fx_setup();
    fx_send("CHAN1234567890:VOLT?");       /* more than 9 digits */
    CHECK_INT(err(), -113);
    fx_send("CHAN:VOLT2?");                /* VOLTage takes no suffix */
    CHECK_INT(err(), -113);
    fx_send("CHAN1X:VOLT?");
    CHECK_INT(err(), -113);
    fx_send("1:VOLT?");
    CHECK_INT(err(), -113);
    fx_send("MEAS1:VOLT:DC?");             /* MEASure has no suffix either */
    CHECK_INT(err(), -113);
    CHECK_STR(fx_out(), "");
    CHECK_INT(viper_scpi_suffix(&g_ctx, 0, 42), 42);
}

/* ---- MIN / MAX / DEF -------------------------------------------------------- */

static void test_keywords_double(void)
{
    fx_setup();
    fx_send("LIM MAX");
    CHECK(g_limit == 10.0);
    fx_send("LIM MIN");
    CHECK(g_limit == 0.0);
    fx_send("LIM DEF");
    CHECK(g_limit == 5.0);
    fx_send("LIM maximum");
    CHECK(g_limit == 10.0);
    fx_send("LIM Minimum");
    CHECK(g_limit == 0.0);
    fx_send("LIM DEFAULT");
    CHECK(g_limit == 5.0);
    fx_send("LIM 7.5");
    CHECK(g_limit == 7.5);
    fx_send("LIM 1E1");
    CHECK(g_limit == 10.0);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);

    fx_send("LIM 10.5");
    CHECK_INT(err(), -222);
    fx_send("LIM -0.1");
    CHECK_INT(err(), -222);
    fx_send("LIM MAXI");
    CHECK_INT(err(), -104);
    fx_send("LIM abc");
    CHECK_INT(err(), -104);
    fx_send("LIM");
    CHECK_INT(err(), -109);
    CHECK(g_limit == 10.0);
}

static void test_keyword_queries(void)
{
    fx_setup();
    fx_send("LIM? MAX");
    CHECK_STR(fx_out(), "10\n");
    fx_clear();
    fx_send("LIM? MIN");
    CHECK_STR(fx_out(), "0\n");
    fx_clear();
    fx_send("LIM? DEF");
    CHECK_STR(fx_out(), "5\n");
    fx_clear();
    fx_send("LIM?");
    CHECK_STR(fx_out(), "1\n");
    fx_clear();
    fx_send("LIM? BOGUS");
    CHECK_INT(err(), -104);
    CHECK_STR(fx_out(), "");
}

static void test_keywords_int(void)
{
    fx_setup();
    fx_send("PCO MAX");
    CHECK_INT(g_pcount, 100);
    fx_send("PCO MIN");
    CHECK_INT(g_pcount, 1);
    fx_send("PCO DEF");
    CHECK_INT(g_pcount, 10);
    fx_send("PCO 0x20");
    CHECK_INT(g_pcount, 32);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
    fx_send("PCO 0");
    CHECK_INT(err(), -222);
    fx_send("PCO 101");
    CHECK_INT(err(), -222);
    fx_send("PCO");
    CHECK_INT(err(), -109);
    CHECK_INT(g_pcount, 32);
    fx_send("PCO 2.5");                   /* rounded */
    CHECK_INT(err(), 0);
    CHECK_INT(g_pcount, 3);
    g_pcount = 32;
}

static void test_keyword_api_arguments(void)
{
    double d;
    int32_t i;

    fx_setup();
    fx_send("PARAMS MAX");
    CHECK_INT(num(&g_ctx, 0, NULL, 1.0, 0.0, 0.5, 0, &d, NULL),
              VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(num(&g_ctx, 0, NULL, 0.0, 1.0, 0.5, 0, NULL, NULL),
              VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(intg(&g_ctx, 0, 5, 1, 3, 0, &i, NULL), VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(intg(&g_ctx, 0, 1, 5, 3, 0, NULL, NULL), VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(num(&g_ctx, 0, NULL, -1.0, 2.0, 0.5, 0, &d, NULL), 0);
    CHECK(d == 2.0);
    CHECK_INT(intg(&g_ctx, 0, -1, 2, 0, 0, &i, NULL), 0);
    CHECK_INT(i, 2);
    CHECK_INT(num(&g_ctx, 3, NULL, 0.0, 1.0, 0.5, 0, &d, NULL),
              VIPER_SCPI_RES_ERR_MISSING_PARAM);
}

/* ---- Block data ------------------------------------------------------------------ */

static void test_block_definite(void)
{
    fx_setup();
    fx_send("BLOCK #15hello");
    CHECK_INT(g_block_len, 5);
    CHECK(memcmp(g_block, "hello", 5) == 0);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
    fx_send("BLOCK?");
    CHECK_STR(fx_out(), "#15hello\n");
    fx_clear();

    fx_send("BLOCK #210abcdefghij");
    CHECK_INT(g_block_len, 10);
    fx_send("BLOCK?");
    CHECK_STR(fx_out(), "#210abcdefghij\n");
    fx_clear();

    fx_send("BLOCK #10");                  /* empty block */
    CHECK_INT(g_block_len, 0);
    fx_send("BLOCK?");
    CHECK_STR(fx_out(), "#10\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_block_binary_content(void)
{
    static const char tail[] = "\nCOUNT 5;COUNT?\n";

    fx_setup();
    /* 9 data bytes: a ; , " ' # 1 CR NL - the NL and CR must not end the line. */
    viper_scpi_input_buffer(&g_ctx, "BLOCK #19a;,\"'#1\r\n", 18);
    CHECK_INT(g_block_len, 0);             /* still assembling: nothing ran yet */
    viper_scpi_input_char(&g_ctx, '\n');   /* now the real terminator */
    CHECK_INT(g_block_len, 9);
    CHECK(memcmp(g_block, "a;,\"'#1\r\n", 9) == 0);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);

    viper_scpi_input_buffer(&g_ctx, tail + 1, sizeof tail - 2);
    CHECK_STR(fx_out(), "5\n");

    /* NUL bytes inside data are kept. */
    fx_clear();
    viper_scpi_input_buffer(&g_ctx, "BLOCK #13a\0b\n", 13);
    CHECK_INT(g_block_len, 3);
    CHECK(g_block[1] == 0);
    fx_send("BLOCK?");
    CHECK(memcmp(fx_out(), "#13a", 4) == 0);
}

static void test_block_whitespace_preserved(void)
{
    fx_setup();
    fx_send("BLOCK #13a  ");                       /* trailing spaces are data */
    CHECK_INT(g_block_len, 3);
    CHECK(memcmp(g_block, "a  ", 3) == 0);
    fx_send("BLOCK   #13 b ;BLOCK?");              /* leading spaces before '#' are not */
    CHECK_STR(fx_out(), "#13 b \n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_block_followed_by_commands(void)
{
    fx_setup();
    fx_send("BLOCK #15hello;BLOCK?;COUNT?");
    CHECK_STR(fx_out(), "#15hello;0\n");
    fx_clear();
    fx_send("PARAMS #13abc,5");
    CHECK_STR(fx_out(), "2\n");
    fx_clear();
    fx_send("PARAMS 1,#13abc");
    CHECK_STR(fx_out(), "2\n");
    fx_clear();
    fx_send("PARAMS 1 , #13abc , 3");
    CHECK_STR(fx_out(), "3\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_block_indefinite(void)
{
    fx_setup();
    fx_send("BLOCK #0xyz;,abc");     /* the rest of the line is data */
    CHECK_INT(g_block_len, 8);
    CHECK(memcmp(g_block, "xyz;,abc", 8) == 0);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
    fx_send("BLOCK #0");
    CHECK_INT(g_block_len, 0);
    fx_send("PARAMS 1,#0abc");
    CHECK_STR(fx_out(), "2\n");
}

static void test_block_not_a_block(void)
{
    fx_setup();
    fx_send("BLOCK 5");
    CHECK_INT(err(), -104);
    fx_send("BLOCK #B101");
    CHECK_INT(err(), -104);
    fx_send("BLOCK \"#15hello\"");
    CHECK_INT(err(), -104);
    fx_send("COUNT #B101;COUNT?");        /* radix prefixes still work */
    CHECK_STR(fx_out(), "5\n");
    fx_clear();
    fx_send("COUNT #HFF;COUNT?");
    CHECK_STR(fx_out(), "255\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_block_hash_in_string(void)
{
    fx_setup();
    /* '#15' inside a quoted string must not swallow the terminator. */
    fx_send("NAME \"x #15\";NAME?");
    CHECK_STR(fx_out(), "\"x #15\"\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_block_errors(void)
{
    fx_setup();
    /* Incomplete definite block, ended by EOM. */
    viper_scpi_input_buffer(&g_ctx, "BLOCK #15hel", 12);
    viper_scpi_end_of_message(&g_ctx);
    CHECK_INT(err(), -161);               /* dropped, command never ran */
    CHECK_INT(g_block_calls, 0);
    fx_send("COUNT 1;COUNT?");            /* parser state fully recovered */
    CHECK_STR(fx_out(), "1\n");
    fx_clear();

    fx_send("BLOCK #15helloX");            /* garbage after the block */
    CHECK_INT(err(), -102);
    fx_send("BLOCK #1a");                  /* malformed length: invalid block */
    CHECK_INT(err(), -161);
    fx_send("COUNT 9;BLOCK #2");           /* LF inside the length digits */
    CHECK_INT(err(), -161);
    CHECK_INT(g_count, 1);                 /* dropped as a whole, like EOM */
    fx_send("COUNT 8;BLOCK #1\r");        /* CR LF likewise */
    CHECK_INT(err(), -161);
    CHECK_INT(g_count, 1);
    g_block_calls = 0;
    viper_scpi_input_buffer(&g_ctx, "BLOCK #233abcde", 15);   /* declares 33, has 5 */
    viper_scpi_end_of_message(&g_ctx);
    CHECK_INT(err(), -161);
    viper_scpi_input_buffer(&g_ctx, "BLOCK #2", 8);   /* EOM inside the length */
    viper_scpi_end_of_message(&g_ctx);
    CHECK_INT(err(), -161);
    viper_scpi_input_buffer(&g_ctx, "BLOCK #13ab", 11);
    viper_scpi_end_of_message(&g_ctx);
    CHECK_INT(err(), -161);
    CHECK_INT(g_block_calls, 0);
    viper_scpi_input_buffer(&g_ctx, "BLOCK #13abc", 12);  /* complete: unaffected */
    viper_scpi_end_of_message(&g_ctx);
    CHECK_INT(g_block_calls, 1);
    CHECK_INT(g_block_len, 3);
    viper_scpi_input_buffer(&g_ctx, "COUNT 9;BLOCK #15ab", 19); /* whole message dropped */
    viper_scpi_end_of_message(&g_ctx);
    CHECK_INT(err(), -161);
    CHECK_INT(g_count, 1);
    CHECK_INT(g_block_calls, 1);
    {
        char msg[64] = "BLOCK #233";
        memset(msg + 10, 'z', 33);
        msg[43] = '\0';
        fx_send(msg);
        CHECK_INT(err(), -222);            /* valid block, too big for the callback */
    }
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_block_overrun_stays_in_sync(void)
{
    char big[VIPER_SCPI_BUFFER_SIZE + 128];
    size_t n = 0;

    fx_setup();
    memcpy(big, "BLOCK #3200", 11);
    n = 11;
    memset(big + n, 'x', 200);
    n += 200;
    big[n++] = '\n';
    viper_scpi_input_buffer(&g_ctx, big, n);
    CHECK_INT(err(), -363);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
    fx_send("COUNT 4;COUNT?");
    CHECK_STR(fx_out(), "4\n");

    /* A newline inside overrun block data does not end the discard early. */
    fx_clear();
    memcpy(big, "BLOCK #3200", 11);
    n = 11;
    memset(big + n, 'x', 200);
    big[n + 100] = '\n';
    n += 200;
    big[n++] = '\n';
    viper_scpi_input_buffer(&g_ctx, big, n);
    CHECK_INT(err(), -363);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
    CHECK_STR(fx_out(), "");
}

static void test_block_result_sizes(void)
{
    static const uint8_t data[123] = { 1 };

    fx_setup();
    viper_scpi_result_block(&g_ctx, data, 123);
    CHECK(strncmp(fx_out(), "#3123", 5) == 0);
    fx_setup();
    viper_scpi_result_block(&g_ctx, data, 0);
    CHECK_STR(fx_out(), "#10");
    fx_setup();
    viper_scpi_result_block(&g_ctx, "ab", 2);
    viper_scpi_result_int(&g_ctx, 7);      /* multiple values: comma separated */
    CHECK_STR(fx_out(), "#12ab,7");
}

static void test_block_api_arguments(void)
{
    const uint8_t *d;
    size_t n;

    fx_setup();
    fx_send("PARAMS #13abc,7");
    CHECK_INT(viper_scpi_param_block(&g_ctx, 0, &d, &n), 0);
    CHECK_INT(n, 3);
    CHECK(memcmp(d, "abc", 3) == 0);
    CHECK_INT(viper_scpi_param_block(&g_ctx, 1, &d, &n), VIPER_SCPI_RES_ERR_DATA_TYPE);
    CHECK_INT(viper_scpi_param_block(&g_ctx, 4, &d, &n), VIPER_SCPI_RES_ERR_MISSING_PARAM);
    CHECK_INT(viper_scpi_param_block(&g_ctx, 0, NULL, &n), VIPER_SCPI_RES_ERR_PARAM_INVALID);
    CHECK_INT(viper_scpi_param_block(&g_ctx, 0, &d, NULL), VIPER_SCPI_RES_ERR_PARAM_INVALID);
}

static void test_lone_hash(void)
{
    /* 5.1, stream model: LF right after '#'. */
    fx_setup();
    fx_send("NAME 'keep'");
    fx_send("NAME #");
    CHECK_INT(err(), -161);
    CHECK_STR(g_name, "keep");
    fx_send("COUNT 5;NAME #");                  /* the whole message is dropped */
    CHECK_INT(err(), -161);
    CHECK_INT(g_count, 0);
    viper_scpi_input_buffer(&g_ctx, "NAME #\r\n", 8);   /* CRLF ends it the same way */
    CHECK_INT(err(), -161);
    viper_scpi_input_buffer(&g_ctx, "NAME #", 6);       /* and so does EOM */
    viper_scpi_end_of_message(&g_ctx);
    CHECK_INT(err(), -161);
    CHECK_STR(g_name, "keep");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
    fx_send("COUNT 6");                          /* next message unaffected */
    CHECK_INT(g_count, 6);

    /* '#' followed by ';', ',' or whitespace: that command fails with -161,
     * the others of the message run. */
    fx_send("NAME #;COUNT 7");
    CHECK_INT(err(), -161);
    CHECK_INT(g_count, 7);
    fx_send("PARAMS 1,#,2");
    CHECK_INT(err(), -161);
    fx_send("PARAMS # 5");
    CHECK_INT(err(), -161);
    fx_send("PARAMS #\t");
    CHECK_INT(err(), -161);
    CHECK_STR(g_name, "keep");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);

    /* Unchanged: '#' + letter reaches the callback; radix numbers; blocks. */
    fx_clear();
    fx_send("PARAMS #x,#H1");
    CHECK_STR(fx_out(), "2\n");
    fx_send("COUNT #HFF");
    CHECK_INT(g_count, 255);
    fx_send("COUNT #B101");
    CHECK_INT(g_count, 5);
    fx_send("COUNT #Q17");
    CHECK_INT(g_count, 15);
    fx_send("BLOCK #13abc");
    CHECK_INT(g_block_len, 3);
    fx_send("BLOCK #0xyz");
    CHECK_INT(g_block_len, 3);
    fx_send("NAME '#'");
    CHECK_STR(g_name, "#");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

int main(void)
{
    test_optional_leaf();
    test_optional_intermediate();
    test_optional_errors();
    test_optional_relative_path();
    test_suffix_values();
    test_suffix_multiple();
    test_suffix_optional_node();
    test_suffix_errors();
    test_keywords_double();
    test_keyword_queries();
    test_keywords_int();
    test_keyword_api_arguments();
    test_block_definite();
    test_block_binary_content();
    test_block_whitespace_preserved();
    test_block_followed_by_commands();
    test_block_indefinite();
    test_block_not_a_block();
    test_block_hash_in_string();
    test_block_errors();
    test_block_overrun_stays_in_sync();
    test_block_result_sizes();
    test_block_api_arguments();
    test_lone_hash();
    return test_summary("test_extras");
}
