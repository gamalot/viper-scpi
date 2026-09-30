#include "test_util.h"

int g_checks;
int g_failures;

viper_scpi_context_t g_ctx;
double g_volt;
double g_curr;
int g_output;
int g_mode;
int32_t g_count;
uint32_t g_ucount;
char g_name[32];
int g_reset_calls;

static char s_out[512];
static size_t s_out_len;

static size_t capture_write(void *user_data, const char *data, size_t len)
{
    (void)user_data;
    if (s_out_len + len < sizeof s_out) {
        memcpy(s_out + s_out_len, data, len);
        s_out_len += len;
        s_out[s_out_len] = '\0';
    }
    return len;
}

static viper_scpi_result_t cb_meas_dc(viper_scpi_context_t *ctx)
{
    if (!viper_scpi_is_query(ctx)) {
        return VIPER_SCPI_RES_ERR_UNDEFINED_HEADER;
    }
    viper_scpi_result_double(ctx, 1.5);
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cb_meas_pair(viper_scpi_context_t *ctx)
{
    viper_scpi_result_int(ctx, 1);
    viper_scpi_result_int(ctx, 2);
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cb_source_volt(viper_scpi_context_t *ctx)
{
    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_double(ctx, g_volt);
        return VIPER_SCPI_RES_OK;
    }
    return viper_scpi_param_double(ctx, 0, &g_volt);
}

static viper_scpi_result_t cb_source_curr(viper_scpi_context_t *ctx)
{
    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_double(ctx, g_curr);
        return VIPER_SCPI_RES_OK;
    }
    return viper_scpi_param_double(ctx, 0, &g_curr);
}

static viper_scpi_result_t cb_output_state(viper_scpi_context_t *ctx)
{
    bool on;
    viper_scpi_result_t res;

    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_bool(ctx, g_output != 0);
        return VIPER_SCPI_RES_OK;
    }
    res = viper_scpi_param_bool(ctx, 0, &on);
    if (res == VIPER_SCPI_RES_OK) {
        g_output = on ? 1 : 0;
    }
    return res;
}

static const viper_scpi_choice_t mode_choices[] = {
    { "NORMal", 0 }, { "FAST", 1 }, { "SLOW", 2 }, { NULL, 0 }
};

static viper_scpi_result_t cb_mode(viper_scpi_context_t *ctx)
{
    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_int(ctx, g_mode);
        return VIPER_SCPI_RES_OK;
    }
    return viper_scpi_param_choice(ctx, 0, mode_choices, &g_mode);
}

static viper_scpi_result_t cb_name(viper_scpi_context_t *ctx)
{
    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_string(ctx, g_name);
        return VIPER_SCPI_RES_OK;
    }
    return viper_scpi_param_string(ctx, 0, g_name, sizeof g_name, NULL);
}

static viper_scpi_result_t cb_count(viper_scpi_context_t *ctx)
{
    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_int(ctx, g_count);
        return VIPER_SCPI_RES_OK;
    }
    return viper_scpi_param_int32(ctx, 0, &g_count);
}

static viper_scpi_result_t cb_ucount(viper_scpi_context_t *ctx)
{
    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_uint(ctx, g_ucount);
        return VIPER_SCPI_RES_OK;
    }
    return viper_scpi_param_uint32(ctx, 0, &g_ucount);
}

static viper_scpi_result_t cb_fail(viper_scpi_context_t *ctx)
{
    (void)ctx;
    return VIPER_SCPI_RES_ERR_EXECUTION;
}

static viper_scpi_result_t cb_params(viper_scpi_context_t *ctx)
{
    viper_scpi_result_int(ctx, (int32_t)viper_scpi_param_count(ctx));
    return VIPER_SCPI_RES_OK;
}

static const viper_scpi_node_t meas_volt_children[] = {
    { "DC", cb_meas_dc, NULL },
    { "PAIR", cb_meas_pair, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t meas_children[] = {
    { "VOLTage", NULL, meas_volt_children },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t source_children[] = {
    { "VOLTage", cb_source_volt, NULL },
    { "CURRent", cb_source_curr, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t output_children[] = {
    { "STATe", cb_output_state, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t display_children[] = {
    { "MODE", cb_mode, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t root[] = {
    { "MEASure", NULL, meas_children },
    { "SOURce", NULL, source_children },
    { "OUTPut", NULL, output_children },
    { "DISPlay", NULL, display_children },
    { "NAME", cb_name, NULL },
    { "COUNt", cb_count, NULL },
    { "UCOunt", cb_ucount, NULL },
    { "FAIL", cb_fail, NULL },
    { "PARAMS", cb_params, NULL },
    VIPER_SCPI_NODE_END
};

static void on_reset(viper_scpi_context_t *ctx)
{
    (void)ctx;
    g_reset_calls++;
}

static const viper_scpi_config_t config = {
    root, capture_write, NULL, on_reset,
    "Acme", "Widget", "SN123", "1.2.3"
};

void fx_setup(void)
{
    g_volt = 0;
    g_curr = 0;
    g_output = 0;
    g_mode = 0;
    g_count = 0;
    g_ucount = 0;
    g_name[0] = '\0';
    g_reset_calls = 0;
    fx_clear();
    (void)viper_scpi_init(&g_ctx, &config);
}

void fx_send(const char *line)
{
    viper_scpi_input_buffer(&g_ctx, line, strlen(line));
    viper_scpi_input_char(&g_ctx, '\n');
}

const char *fx_out(void)
{
    return s_out;
}

void fx_clear(void)
{
    s_out[0] = '\0';
    s_out_len = 0;
}

int test_summary(const char *suite)
{
    printf("%s: %d checks, %d failures\n", suite, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
