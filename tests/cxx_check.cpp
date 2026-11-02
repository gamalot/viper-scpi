// CI check: the headers compile as C++ and a C++ callback can return any
// SCPI error number, device-defined ones included, without casts.
#include "viper_scpi/viper_scpi.h"

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
    viper_scpi_input_buffer(&ctx, "REL 3\n", 6);
    return viper_scpi_error_pop(&ctx) == 101 ? 0 : 1;
}
