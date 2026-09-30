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
double g_limit;
int32_t g_pcount;
uint8_t g_block[32];
size_t g_block_len;
double g_num;
double g_vlim;

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

static viper_scpi_result_t cb_temp(viper_scpi_context_t *ctx)
{
    viper_scpi_result_int(ctx, 25);
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cb_power(viper_scpi_context_t *ctx)
{
    viper_scpi_result_text(ctx, "POWER");
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cb_prot(viper_scpi_context_t *ctx)
{
    viper_scpi_result_text(ctx, "PROT");
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cb_chan_volt(viper_scpi_context_t *ctx)
{
    viper_scpi_result_int(ctx, viper_scpi_suffix(ctx, 0, 1));
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cb_chan_range(viper_scpi_context_t *ctx)
{
    viper_scpi_result_int(ctx, viper_scpi_suffix(ctx, 0, 1));
    viper_scpi_result_int(ctx, viper_scpi_suffix(ctx, 1, 1));
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cb_bank_id(viper_scpi_context_t *ctx)
{
    viper_scpi_result_int(ctx, viper_scpi_suffix(ctx, 0, 1));
    viper_scpi_result_int(ctx, viper_scpi_suffix(ctx, 1, 1));
    viper_scpi_result_int(ctx, viper_scpi_suffix(ctx, 9, -7)); /* out of range */
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cb_limit(viper_scpi_context_t *ctx)
{
    double v = g_limit;

    if (viper_scpi_is_query(ctx)) {
        if (viper_scpi_param_present(ctx, 0)) {
            viper_scpi_result_t res = viper_scpi_param_double_kw(ctx, 0, 0.0, 10.0, 5.0, &v);
            if (res != VIPER_SCPI_RES_OK) {
                return res;
            }
        }
        viper_scpi_result_double(ctx, v);
        return VIPER_SCPI_RES_OK;
    }
    return viper_scpi_param_double_kw(ctx, 0, 0.0, 10.0, 5.0, &g_limit);
}

static viper_scpi_result_t cb_pcount(viper_scpi_context_t *ctx)
{
    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_int(ctx, g_pcount);
        return VIPER_SCPI_RES_OK;
    }
    return viper_scpi_param_int32_kw(ctx, 0, 1, 100, 10, &g_pcount);
}

static viper_scpi_result_t cb_block(viper_scpi_context_t *ctx)
{
    const uint8_t *data;
    size_t len;
    viper_scpi_result_t res;

    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_block(ctx, g_block, g_block_len);
        return VIPER_SCPI_RES_OK;
    }
    res = viper_scpi_param_block(ctx, 0, &data, &len);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (len > sizeof g_block) {
        return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
    }
    memcpy(g_block, data, len);
    g_block_len = len;
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cb_trig_src(viper_scpi_context_t *ctx)
{
    viper_scpi_result_text(ctx, "TRIGSRC");
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cb_calc_state(viper_scpi_context_t *ctx)
{
    viper_scpi_result_int(ctx, viper_scpi_suffix(ctx, 0, 1));
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cb_route_layer(viper_scpi_context_t *ctx)
{
    viper_scpi_result_text(ctx, "LAYER");
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cb_route_count(viper_scpi_context_t *ctx)
{
    viper_scpi_result_text(ctx, "COUNT");
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cb_broken(viper_scpi_context_t *ctx)
{
    viper_scpi_result_text(ctx, "BROKEN");
    return VIPER_SCPI_RES_OK;
}

/* Generic setter/getter for a number with a unit table. */
static viper_scpi_result_t unit_cmd(viper_scpi_context_t *ctx, const viper_scpi_unit_t *units)
{
    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_double(ctx, g_num);
        return VIPER_SCPI_RES_OK;
    }
    return viper_scpi_param_double_unit(ctx, 0, units, &g_num);
}

static viper_scpi_result_t cb_freq(viper_scpi_context_t *ctx)
{
    return unit_cmd(ctx, viper_scpi_units_hertz);
}

static viper_scpi_result_t cb_res(viper_scpi_context_t *ctx)
{
    return unit_cmd(ctx, viper_scpi_units_ohm);
}

static viper_scpi_result_t cb_amps(viper_scpi_context_t *ctx)
{
    return unit_cmd(ctx, viper_scpi_units_amp);
}

static viper_scpi_result_t cb_cap(viper_scpi_context_t *ctx)
{
    return unit_cmd(ctx, viper_scpi_units_farad);
}

static viper_scpi_result_t cb_time(viper_scpi_context_t *ctx)
{
    return unit_cmd(ctx, viper_scpi_units_second);
}

static viper_scpi_result_t cb_ctmp(viper_scpi_context_t *ctx)
{
    return unit_cmd(ctx, viper_scpi_units_celsius);
}

static viper_scpi_result_t cb_watt(viper_scpi_context_t *ctx)
{
    return unit_cmd(ctx, viper_scpi_units_watt);
}

static viper_scpi_result_t cb_ind(viper_scpi_context_t *ctx)
{
    return unit_cmd(ctx, viper_scpi_units_henry);
}

static viper_scpi_result_t cb_gain(viper_scpi_context_t *ctx)
{
    return unit_cmd(ctx, viper_scpi_units_decibel);
}

static viper_scpi_result_t cb_vlim(viper_scpi_context_t *ctx)
{
    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_double(ctx, g_vlim);
        return VIPER_SCPI_RES_OK;
    }
    return viper_scpi_param_double_unit_kw(ctx, 0, viper_scpi_units_volt, -10.0, 10.0, 1.0,
                                           &g_vlim);
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
static const viper_scpi_node_t sense_children[] = {
    { "TEMPerature", cb_temp, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t power_children[] = {
    { "[LEVel]", cb_power, NULL },
    { "PROTection", cb_prot, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t bank_children[] = {
    { "ID", cb_bank_id, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t chan_children[] = {
    { "VOLTage", cb_chan_volt, NULL },
    { "RANGe#", cb_chan_range, NULL },
    { "[BANK#]", NULL, bank_children },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t route_layer_children[] = {
    { "COUNt", cb_route_count, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t root[] = {
    { "TRIGger[:SEQuence]:SOURce", cb_trig_src, NULL },
    { "CALCulate#[:MATH]:STATe", cb_calc_state, NULL },
    { "ROUTe:LAYer[:SUB]", cb_route_layer, route_layer_children },
    { "BROKen[:X", cb_broken, NULL },
    { "[]", cb_broken, NULL },
    { "FREQuency", cb_freq, NULL },
    { "RESistance", cb_res, NULL },
    { "AMPS", cb_amps, NULL },
    { "CAPacitance", cb_cap, NULL },
    { "TIMe", cb_time, NULL },
    { "CTMP", cb_ctmp, NULL },
    { "WATT", cb_watt, NULL },
    { "IND", cb_ind, NULL },
    { "GAIN", cb_gain, NULL },
    { "VLIMit", cb_vlim, NULL },
    { "[SENSe]", NULL, sense_children },
    { "POWer", NULL, power_children },
    { "CHANnel#", NULL, chan_children },
    { "LIMit", cb_limit, NULL },
    { "PCOunt", cb_pcount, NULL },
    { "BLOCk", cb_block, NULL },
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
    g_limit = 1.0;
    g_num = 0;
    g_vlim = 1.0;
    g_pcount = 0;
    g_block_len = 0;
    memset(g_block, 0, sizeof g_block);
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
