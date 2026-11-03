// CI check: the headers compile as C++ and a C++ callback can return any
// SCPI error number, device-defined ones included, without casts. Built as
// C++11, where specs are written by position, and as C++20, where the
// designated form of the C documentation works (in declaration order).
#include "viper_scpi/viper_scpi.h"

#if __cplusplus >= 202002L
// C++ needs the members in declaration order, and -Wextra wants them all.
static const viper_scpi_integer_t mv = {
    .min = 0, .max = 30000, .def = 0, .units = &viper_scpi_units_volt, .flags = 0, .exp10 = -3
};
static const viper_scpi_number_t volt = {
    .min = 0.0, .max = 30.0, .def = 5.0, .units = nullptr, .flags = 0
};
#else
static const viper_scpi_integer_t mv = { 0, 30000, 0, &viper_scpi_units_volt, 0, -3 };
static const viper_scpi_number_t volt = { 0.0, 30.0, 5.0, nullptr, 0 };
#endif

static int32_t g_mv;
static double g_volt;

static viper_scpi_result_t cb_volt(viper_scpi_context_t *ctx)
{
    return viper_scpi_param_integer(ctx, 0, &mv, &g_mv, nullptr);
}

static viper_scpi_result_t cb_limit(viper_scpi_context_t *ctx)
{
    return viper_scpi_param_number(ctx, 0, &volt, &g_volt, nullptr);
}

static viper_scpi_result_t cb_relay(viper_scpi_context_t *ctx)
{
    int32_t v = 0;
    viper_scpi_result_t res = viper_scpi_param_int32(ctx, 0, &v);

    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (v == 3) {
        return 101; // device-defined error
    }
    if (v > 3) {
        return viper_scpi_fail(ctx, VIPER_SCPI_RES_ERR_OUT_OF_RANGE, "K1..K3");
    }
    return VIPER_SCPI_RES_OK;
}

static const viper_scpi_node_t root[] = {
    { "RELay", cb_relay, nullptr },
    { "VOLTage", cb_volt, nullptr },
    { "LIMit", cb_limit, nullptr },
    VIPER_SCPI_NODE_END
};

static const viper_scpi_error_text_t texts[] = { { 101, "Relay stuck" }, { 0, nullptr } };

int main()
{
    static viper_scpi_config_t cfg = {};
    static viper_scpi_context_t ctx;

    cfg.root = root;
    cfg.error_texts = texts;
    if (viper_scpi_init(&ctx, &cfg) != VIPER_SCPI_RES_OK) {
        return 1;
    }
    viper_scpi_input_buffer(&ctx, "REL 3;VOLT 1.5;LIM MAX\n", 23);
    return (viper_scpi_error_pop(&ctx) == 101 && g_mv == 1500 && g_volt == 30.0) ? 0 : 1;
}
