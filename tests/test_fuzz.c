/*
 * Deterministic fuzz smoke test: random bytes drawn from a protocol-heavy
 * alphabet are fed to a parser with a tree that exercises optional nodes,
 * suffixes, blocks, keywords and units. The test passes if nothing crashes;
 * it is most useful under ASan/UBSan.
 */
#include <stdio.h>
#include <string.h>

#include "viper_scpi/viper_scpi.h"

static size_t sink(void *user_data, const char *data, size_t len)
{
    (void)user_data;
    (void)data;
    return len;
}

static viper_scpi_result_t cb(viper_scpi_context_t *ctx)
{
    const uint8_t *blk;
    size_t n;
    double d;
    int32_t i;
    uint32_t u;
    bool b;
    char s[16];
    static const viper_scpi_choice_t ch[] = { { "ONE", 1 }, { "TWO", 2 }, { NULL, 0 } };
    int c;

    (void)viper_scpi_param_block(ctx, 0, &blk, &n);
    (void)viper_scpi_param_double(ctx, 0, &d);
    (void)viper_scpi_param_double_kw(ctx, 1, 0.0, 1.0, 0.5, &d);
    (void)viper_scpi_param_double_unit(ctx, 0, viper_scpi_units_hertz, &d);
    (void)viper_scpi_param_double_unit_kw(ctx, 1, viper_scpi_units_second, 0.0, 1.0, 0.5, &d);
    (void)viper_scpi_param_int32(ctx, 0, &i);
    (void)viper_scpi_param_int32_kw(ctx, 1, 0, 9, 1, &i);
    (void)viper_scpi_param_uint32(ctx, 2, &u);
    (void)viper_scpi_param_bool(ctx, 0, &b);
    (void)viper_scpi_param_choice(ctx, 0, ch, &c);
    (void)viper_scpi_param_string(ctx, 0, s, sizeof s, &n);
    viper_scpi_result_int(ctx, viper_scpi_suffix(ctx, 0, 1));
    viper_scpi_result_string(ctx, "a\"b");
    viper_scpi_result_block(ctx, "xyz", 3);
    return VIPER_SCPI_RES_OK;
}

static const viper_scpi_node_t leaf[] = {
    { "X", cb, NULL },
    { "Y[:Z][:W#]:Q", cb, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t root[] = {
    { "[SENSe]", NULL, leaf },
    { "CHANnel#[:AB]", cb, leaf },
    { "A[:B", cb, NULL },      /* malformed on purpose */
    { "[]", cb, leaf },        /* malformed on purpose */
    { "A", NULL, leaf },
    { "[:A][:B]", cb, leaf },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_config_t config = { root, sink, NULL, NULL, NULL, NULL, NULL, NULL };

static uint32_t rng_state = 2463534242u;

static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

int main(void)
{
    static viper_scpi_context_t ctx;
    static const char alphabet[] =
        "#0123456789 ,;:?*\"'\nXYZWQAaCHANnelSENSEBHZKMUVSECMIN\r-+.eE"
        "IDN?*RSTCLSOPCESRSTBSYST:ERR?";
    const unsigned long iterations = 1000000ul;

    if (viper_scpi_init(&ctx, &config) != VIPER_SCPI_RES_OK) {
        return 1;
    }
    for (unsigned long i = 0; i < iterations; i++) {
        char c;

        if (rng() % 8 == 0) {
            c = (char)rng();
        } else {
            c = alphabet[rng() % (sizeof alphabet - 1)];
        }
        viper_scpi_input_char(&ctx, c);
        if (rng() % 5000 == 0) {
            viper_scpi_end_of_message(&ctx);
        }
        if (rng() % 20000 == 0) {
            (void)viper_scpi_status_byte(&ctx);
            (void)viper_scpi_error_pop(&ctx);
        }
    }
    printf("test_fuzz: %lu bytes processed\n", iterations);
    return 0;
}
