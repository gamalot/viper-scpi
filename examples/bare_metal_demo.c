/*
 * Minimal bare-metal style demo. A simulated UART delivers a byte stream in
 * small chunks; responses are written to stdout. On real hardware replace
 * uart_read()/uart_write() with the transport driver.
 */
#include <stdio.h>
#include <string.h>

#include "viper_scpi/viper_scpi.h"

static double g_voltage;
static bool g_output;

static viper_scpi_result_t cmd_meas_volt_dc(viper_scpi_context_t *ctx)
{
    if (!viper_scpi_is_query(ctx)) {
        return VIPER_SCPI_RES_ERR_UNDEFINED_HEADER;
    }
    viper_scpi_result_double(ctx, g_output ? g_voltage : 0.0);
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_source_volt(viper_scpi_context_t *ctx)
{
    double v;
    viper_scpi_result_t res;

    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_double(ctx, g_voltage);
        return VIPER_SCPI_RES_OK;
    }
    res = viper_scpi_param_double(ctx, 0, &v);
    if (res != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (v < 0.0 || v > 30.0) {
        return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
    }
    g_voltage = v;
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_output_state(viper_scpi_context_t *ctx)
{
    bool on;
    viper_scpi_result_t res;

    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_bool(ctx, g_output);
        return VIPER_SCPI_RES_OK;
    }
    res = viper_scpi_param_bool(ctx, 0, &on);
    if (res == VIPER_SCPI_RES_OK) {
        g_output = on;
    }
    return res;
}

/* The command tree lives in const memory (Flash on a microcontroller). */
static const viper_scpi_node_t meas_volt_nodes[] = {
    { "DC", cmd_meas_volt_dc, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t meas_nodes[] = {
    { "VOLTage", NULL, meas_volt_nodes },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t source_nodes[] = {
    { "VOLTage", cmd_source_volt, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t output_nodes[] = {
    { "STATe", cmd_output_state, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t root_nodes[] = {
    { "MEASure", NULL, meas_nodes },
    { "SOURce", NULL, source_nodes },
    { "OUTPut", NULL, output_nodes },
    VIPER_SCPI_NODE_END
};

static void reset(viper_scpi_context_t *ctx)
{
    (void)ctx;
    g_voltage = 0.0;
    g_output = false;
}

static size_t uart_write(void *user_data, const char *data, size_t len)
{
    (void)user_data;
    return fwrite(data, 1, len, stdout);
}

static const viper_scpi_config_t config = {
    .root = root_nodes,
    .write = uart_write,
    .reset = reset,
    .idn_manufacturer = "Viper",
    .idn_model = "Demo PSU",
    .idn_serial = "0001",
    .idn_firmware = "0.1.0",
};

static viper_scpi_context_t scpi; /* static: no heap, no stack pressure */

int main(void)
{
    static const char stream[] =
        "*IDN?\n"
        "SOUR:VOLT 12.5;:OUTP:STAT ON\n"
        "MEAS:VOLT:DC?;:SOUR:VOLT?\n"
        "SOUR:VOLT 99\n"
        "SYST:ERR?\n"
        "*RST;OUTP:STAT?\n";
    const size_t chunk = 7; /* mimic small UART/USB packets */

    if (viper_scpi_init(&scpi, &config) != VIPER_SCPI_RES_OK) {
        return 1;
    }
    for (size_t off = 0; off < sizeof stream - 1; off += chunk) {
        size_t n = sizeof stream - 1 - off;
        viper_scpi_input_buffer(&scpi, stream + off, n < chunk ? n : chunk);
    }
    return 0;
}
