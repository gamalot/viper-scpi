/* Opt-in leniencies (viper_scpi_config_t.options), on and off. */
#include "test_util.h"

static double s_volt, s_curr;
static char s_buf[128];

static viper_scpi_result_t cb_volt(viper_scpi_context_t *ctx)
{
    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_double(ctx, s_volt);
        return VIPER_SCPI_RES_OK;
    }
    return viper_scpi_param_double(ctx, 0, &s_volt);
}

static viper_scpi_result_t cb_curr(viper_scpi_context_t *ctx)
{
    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_double(ctx, s_curr);
        return VIPER_SCPI_RES_OK;
    }
    return viper_scpi_param_double(ctx, 0, &s_curr);
}

static const viper_scpi_node_t source_nodes[] = {
    { "VOLTage", cb_volt, NULL },
    { "CURRent", cb_curr, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t root[] = {
    { "SOURce", NULL, source_nodes },
    VIPER_SCPI_NODE_END
};

static const viper_scpi_config_t cfg_std = {
    .root = root, .output_buffer = s_buf, .output_size = sizeof s_buf,
    .idn_manufacturer = "A", .idn_model = "B", .idn_serial = "C", .idn_firmware = "D",
};
static const viper_scpi_config_t cfg_rel = {
    .root = root, .output_buffer = s_buf, .output_size = sizeof s_buf,
    .options = VIPER_SCPI_OPT_RELATIVE_FALLBACK,
};
static const viper_scpi_config_t cfg_eom = {
    .root = root, .output_buffer = s_buf, .output_size = sizeof s_buf,
    .idn_manufacturer = "A", .idn_model = "B", .idn_serial = "C", .idn_firmware = "D",
    .options = VIPER_SCPI_OPT_EOM_MESSAGES,
};

static viper_scpi_context_t q;

static void setup(const viper_scpi_config_t *cfg)
{
    s_volt = s_curr = 0;
    (void)viper_scpi_init(&q, cfg);
}

static void send(const char *s)
{
    viper_scpi_input_buffer(&q, s, strlen(s));
    viper_scpi_end_of_message(&q);
}

static const char *reply(void)
{
    static char out[128];
    size_t n = 0;
    bool end = false;

    while (!end) {
        size_t k = viper_scpi_output_read(&q, (uint8_t *)out + n, sizeof out - 1 - n, &end);
        n += k;
        if (k == 0) {
            break;
        }
    }
    out[n] = '\0';
    return out;
}

static void test_relative_fallback(void)
{
    /* Off (standard): the second unit means SOUR:SOUR:CURR. */
    setup(&cfg_std);
    send("SOUR:VOLT 1;SOUR:CURR 2");
    CHECK(s_volt == 1.0);
    CHECK(s_curr == 0.0);
    CHECK_INT(viper_scpi_error_pop(&q), -113);

    /* On: retried from the root. */
    setup(&cfg_rel);
    send("SOUR:VOLT 1;SOUR:CURR 2");
    CHECK(s_curr == 2.0);
    CHECK_INT(viper_scpi_error_count(&q), 0);
    send("SOUR:VOLT 3;CURR 4");           /* proper relative headers still work */
    CHECK(s_volt == 3.0);
    CHECK(s_curr == 4.0);
    send("SOUR:VOLT 1;SOUR:CURR 5;VOLT 6"); /* the path follows the retried header */
    CHECK(s_curr == 5.0);
    CHECK(s_volt == 6.0);
    send("SOUR:VOLT 1;BOGUS 2");          /* unresolvable either way */
    CHECK_INT(viper_scpi_error_pop(&q), -113);
    send("SOUR:VOLT 1;SYST:ERR?");        /* built-in tree reached too */
    CHECK_STR(reply(), "0,\"No error\"\n");
    CHECK_INT(viper_scpi_error_count(&q), 0);
}

/* The inherited path counts toward VIPER_SCPI_MAX_RECURSION_DEPTH; with the
 * fallback a relative header that only fits from the root is still found. */
static viper_scpi_result_t cb_ok(viper_scpi_context_t *ctx)
{
    viper_scpi_result_text(ctx, "OK");
    return VIPER_SCPI_RES_OK;
}

static void test_fallback_past_depth(void)
{
    static const viper_scpi_node_t y_nodes[] = { { "Z", cb_ok, NULL }, VIPER_SCPI_NODE_END };
    static const viper_scpi_node_t deep_root[] = {
        { "A:B:C:D:E:F:G:H", cb_ok, NULL },  /* 8 mnemonics: a 7-deep path */
        { "Y", NULL, y_nodes },
        VIPER_SCPI_NODE_END
    };
    static const viper_scpi_config_t cfg_deep_rel = {
        .root = deep_root, .output_buffer = s_buf, .output_size = sizeof s_buf,
        .options = VIPER_SCPI_OPT_RELATIVE_FALLBACK,
    };
    static const viper_scpi_config_t cfg_deep = {
        .root = deep_root, .output_buffer = s_buf, .output_size = sizeof s_buf,
    };

    setup(&cfg_deep_rel);
    send("A:B:C:D:E:F:G:H?;Y:Z?");
    CHECK_STR(reply(), "OK;OK\n");
    CHECK_INT(viper_scpi_error_count(&q), 0);

    setup(&cfg_deep);                     /* standard: A:...:G:Y:Z is too deep */
    send("A:B:C:D:E:F:G:H?;Y:Z?");
    CHECK_STR(reply(), "OK\n");
    CHECK_INT(viper_scpi_error_pop(&q), -112);
}

static void test_eom_messages(void)
{
    /* Off (standard): each LF ends a program message; the second query
     * interrupts the first reply. */
    setup(&cfg_std);
    send("*IDN?\n*OPC?\n");
    CHECK_INT(viper_scpi_error_pop(&q), -410);
    CHECK_STR(reply(), "1\n");

    /* On: one program message per EOM, responses joined by ';'. */
    setup(&cfg_eom);
    send("*IDN?\n*OPC?\n");
    CHECK_INT(viper_scpi_error_count(&q), 0);
    CHECK_STR(reply(), "A,B,C,D;1\n");
    send("SOUR:VOLT 2\nSOUR:CURR 3\n\nSOUR:VOLT?;CURR?");
    CHECK_STR(reply(), "2;3\n");
    CHECK_INT(viper_scpi_error_count(&q), 0);
    send("SOUR:VOLT 2\nCURR 4");           /* a new line starts at the root */
    CHECK_INT(viper_scpi_error_pop(&q), -113);
    CHECK(s_curr == 3.0);
    send("*OPC?");                         /* no LF at all */
    CHECK_STR(reply(), "1\n");

    /* Blocks and strings keep their LFs. */
    send("SOUR:VOLT #12\n1\n*OPC?");       /* "#12" block holding LF+'1' */
    CHECK_INT(viper_scpi_error_pop(&q), -104); /* VOLT takes no block */
    CHECK_STR(reply(), "1\n");

    /* The stream model ignores the option: LF always ends the message. */
    {
        static const viper_scpi_config_t cfg_stream_eom = {
            .root = root, .options = VIPER_SCPI_OPT_EOM_MESSAGES,
        };
        (void)viper_scpi_init(&q, &cfg_stream_eom);
        viper_scpi_input_buffer(&q, "SOUR:VOLT 7\nCURR 8\n", 19);
        CHECK(s_volt == 7.0);
        CHECK_INT(viper_scpi_error_pop(&q), -113);
    }
}

static void test_lone_hash_with_eom_messages(void)
{
    /* 5.1 under VIPER_SCPI_OPT_EOM_MESSAGES: an LF after '#' only ends the
     * command, so only that command fails. */
    setup(&cfg_eom);
    send("SOUR:VOLT #\nSOUR:CURR 3");
    CHECK_INT(viper_scpi_error_pop(&q), -161);
    CHECK(s_volt == 0.0);
    CHECK(s_curr == 3.0);
    send("SOUR:VOLT #");                       /* EOM right after '#' */
    CHECK_INT(viper_scpi_error_pop(&q), -161);
    CHECK_INT(viper_scpi_error_count(&q), 0);
}

int main(void)
{
    test_relative_fallback();
    test_fallback_past_depth();
    test_eom_messages();
    test_lone_hash_with_eom_messages();
    return test_summary("test_options");
}
