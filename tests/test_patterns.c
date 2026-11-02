/* Multi-mnemonic node patterns with optional parts: A[:B]:C. */
#include "test_util.h"

static int err(void)
{
    return viper_scpi_error_pop(&g_ctx);
}

static void test_optional_middle(void)
{
    fx_setup();
    fx_send("TRIG:SOUR?");
    CHECK_STR(fx_out(), "TRIGSRC\n");
    fx_clear();
    fx_send("TRIG:SEQ:SOUR?");
    CHECK_STR(fx_out(), "TRIGSRC\n");
    fx_clear();
    fx_send("trigger:sequence:source?");
    CHECK_STR(fx_out(), "TRIGSRC\n");
    fx_clear();
    fx_send(":TRIGGER:SOURCE?");
    CHECK_STR(fx_out(), "TRIGSRC\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_mandatory_parts_required(void)
{
    fx_setup();
    fx_send("TRIG?");            /* SOURce is mandatory */
    CHECK_INT(err(), -113);
    fx_send("TRIG:SEQ?");
    CHECK_INT(err(), -113);
    fx_send("SEQ:SOUR?");        /* first mnemonic is mandatory too */
    CHECK_INT(err(), -113);
    fx_send("SOUR?");
    CHECK_INT(err(), -113);
    fx_send("TRIG:SEQ:SEQ:SOUR?");
    CHECK_INT(err(), -113);
    fx_send("TRIG:SOUR:X?");     /* extra mnemonic beyond a leaf */
    CHECK_INT(err(), -113);
    fx_send("TRIG:SEQ:SOUR:?");
    CHECK_INT(err(), -102);
    CHECK_STR(fx_out(), "");
}

static void test_suffix_in_multi_mnemonic_pattern(void)
{
    fx_setup();
    fx_send("CALC:STAT?");
    CHECK_STR(fx_out(), "1\n");
    fx_clear();
    fx_send("CALC3:STAT?");
    CHECK_STR(fx_out(), "3\n");
    fx_clear();
    fx_send("CALC2:MATH:STAT?");
    CHECK_STR(fx_out(), "2\n");
    fx_clear();
    fx_send("CALC:MATH:STAT?");
    CHECK_STR(fx_out(), "1\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
    fx_send("CALC:MATH2:STAT?");           /* MATH has no suffix */
    CHECK_INT(err(), -113);
    fx_send("CALC:MATH?");
    CHECK_INT(err(), -113);
}

static void test_pattern_with_children(void)
{
    fx_setup();
    fx_send("ROUT:LAY?");
    CHECK_STR(fx_out(), "LAYER\n");
    fx_clear();
    fx_send("ROUT:LAY:SUB?");
    CHECK_STR(fx_out(), "LAYER\n");
    fx_clear();
    fx_send("ROUT:LAY:COUN?");
    CHECK_STR(fx_out(), "COUNT\n");
    fx_clear();
    fx_send("ROUT:LAY:SUB:COUN?");
    CHECK_STR(fx_out(), "COUNT\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
    fx_clear();
    fx_send("ROUT?");
    CHECK_INT(err(), -113);
    fx_send("ROUT:COUN?");
    CHECK_INT(err(), -113);
    fx_send("ROUT:LAY:SUB:SUB?");
    CHECK_INT(err(), -113);
    CHECK_STR(fx_out(), "");
}

static void test_relative_path_after_multi_mnemonic(void)
{
    fx_setup();
    /* Relative headers extend the previous path minus its last mnemonic. */
    fx_send("ROUT:LAY?;LAY:SUB?");
    CHECK_STR(fx_out(), "LAYER;LAYER\n");
    fx_clear();
    fx_send("TRIG:SEQ:SOUR?;:TRIG:SOUR?");
    CHECK_STR(fx_out(), "TRIGSRC;TRIGSRC\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
    fx_clear();

    /* The path is everything but the last written mnemonic. */
    fx_send("TRIG:SEQ:SOUR?;SOUR?");        /* TRIG:SEQ + SOUR */
    CHECK_STR(fx_out(), "TRIGSRC;TRIGSRC\n");
    fx_clear();
    fx_send("TRIG:SOUR?;SEQ:SOUR?");        /* TRIG + SEQ:SOUR */
    CHECK_STR(fx_out(), "TRIGSRC;TRIGSRC\n");
    fx_clear();
    fx_send("CALC2:MATH:STAT?;STAT?");      /* suffixes of the prefix are kept */
    CHECK_STR(fx_out(), "2;2\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
    fx_send("TRIG:SEQ:SOUR?;TRIG:SOUR?");   /* TRIG:SEQ:TRIG:SOUR does not exist */
    CHECK_INT(err(), -113);
    fx_send("TRIG:SOUR?;LAY?");             /* nor does TRIG:LAY */
    CHECK_INT(err(), -113);
}

static void test_malformed_patterns_never_match(void)
{
    fx_setup();
    fx_send("BROK?");            /* pattern "BROKen[:X" has an unclosed bracket */
    CHECK_INT(err(), -113);
    fx_send("BROK:X?");
    CHECK_INT(err(), -113);
    fx_send("BROKEN?");
    CHECK_INT(err(), -113);
    CHECK_STR(fx_out(), "");
}

static void test_existing_single_item_forms_still_work(void)
{
    fx_setup();
    fx_send("POW?");
    CHECK_STR(fx_out(), "POWER\n");
    fx_clear();
    fx_send("SENS:TEMP?");
    CHECK_STR(fx_out(), "25\n");
    fx_clear();
    fx_send("CHAN2:BANK3:ID?");
    CHECK_STR(fx_out(), "2,3,-7\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static viper_scpi_result_t deep_leaf(viper_scpi_context_t *ctx)
{
    for (size_t i = 0; i < 6; i++) {
        viper_scpi_result_int(ctx, viper_scpi_suffix(ctx, i, 0));
    }
    return VIPER_SCPI_RES_OK;
}

static const viper_scpi_node_t deep_e[] = { { "LEAF", deep_leaf, NULL }, VIPER_SCPI_NODE_END };
static const viper_scpi_node_t deep_root[] = {
    { "A#:B#:C#:D#:E#", NULL, deep_e },
    { "P#:Q#:R#:S#[:T#]", deep_leaf, NULL },
    VIPER_SCPI_NODE_END
};

static char deep_out[128];
static size_t deep_len;

static size_t deep_write(void *user, const char *data, size_t len)
{
    (void)user;
    if (deep_len + len < sizeof deep_out) {
        memcpy(deep_out + deep_len, data, len);
        deep_len += len;
        deep_out[deep_len] = '\0';
    }
    return len;
}

/* A path with more '#' mnemonics than VIPER_SCPI_MAX_SUFFIXES still
 * matches; only the first ones are recorded, the others read as the
 * default. */
static void test_more_suffixes_than_recorded(void)
{
    static const viper_scpi_config_t cfg = { .root = deep_root, .write = deep_write };
    viper_scpi_context_t c;

    (void)viper_scpi_init(&c, &cfg);
    viper_scpi_input_buffer(&c, "A1:B2:C3:D4:E5:LEAF?\n", 21);
    CHECK_INT(viper_scpi_error_pop(&c), 0);
    CHECK_STR(deep_out, VIPER_SCPI_MAX_SUFFIXES == 4 ? "1,2,3,4,0,0\n" : deep_out);
    deep_len = 0;
    deep_out[0] = '\0';
    viper_scpi_input_buffer(&c, "P1:Q2:R3:S4?\n", 13);
    CHECK_INT(viper_scpi_error_pop(&c), 0);
    CHECK_STR(deep_out, VIPER_SCPI_MAX_SUFFIXES == 4 ? "1,2,3,4,0,0\n" : deep_out);
}

int main(void)
{
    test_optional_middle();
    test_mandatory_parts_required();
    test_suffix_in_multi_mnemonic_pattern();
    test_pattern_with_children();
    test_relative_path_after_multi_mnemonic();
    test_malformed_patterns_never_match();
    test_existing_single_item_forms_still_work();
    test_more_suffixes_than_recorded();
    return test_summary("test_patterns");
}
