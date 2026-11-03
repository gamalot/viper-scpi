/* Built with VIPER_SCPI_BUFFER_SIZE=65535, the largest allowed: the 16-bit
 * line offsets at their limit. */
#include <stdlib.h>

#include "test_util.h"

#if VIPER_SCPI_BUFFER_SIZE != 65535
#error "test_bigbuf is built with VIPER_SCPI_BUFFER_SIZE=65535"
#endif

static viper_scpi_context_t c;
static char s_out[64];
static size_t s_out_len;
static int s_runs, s_measures;

static size_t out(void *user, const char *data, size_t len)
{
    (void)user;
    if (s_out_len + len < sizeof s_out) {
        memcpy(s_out + s_out_len, data, len);
        s_out_len += len;
        s_out[s_out_len] = '\0';
    }
    return len;
}

/* Starts an operation, like INIT. */
static viper_scpi_result_t cb_init(viper_scpi_context_t *ctx)
{
    s_runs++;
    viper_scpi_op_begin(ctx);
    return VIPER_SCPI_RES_OK;
}

/* A slow query: deferred once, answered on the re-run. */
static viper_scpi_result_t cb_meas(viper_scpi_context_t *ctx)
{
    s_measures++;
    if (!viper_scpi_resumed(ctx)) {
        viper_scpi_defer(ctx);
        return VIPER_SCPI_RES_OK;
    }
    viper_scpi_result_int(ctx, 42);
    return VIPER_SCPI_RES_OK;
}

static const viper_scpi_node_t root[] = {
    { "INIT", cb_init, NULL },
    { "MEASure", cb_meas, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_config_t cfg = { .root = root, .write = out };

/* Sends <head><spaces><tail>, exactly VIPER_SCPI_BUFFER_SIZE bytes, then LF:
 * a message that fills the line buffer. */
static void send_full(const char *head, const char *tail)
{
    static char msg[VIPER_SCPI_BUFFER_SIZE + 1];
    size_t h = strlen(head), t = strlen(tail);

    memcpy(msg, head, h);
    memset(msg + h, ' ', VIPER_SCPI_BUFFER_SIZE - h - t);
    memcpy(msg + VIPER_SCPI_BUFFER_SIZE - t, tail, t);
    msg[VIPER_SCPI_BUFFER_SIZE] = '\n';
    viper_scpi_input_buffer(&c, msg, sizeof msg);
}

static void setup(void)
{
    s_out[0] = '\0';
    s_out_len = 0;
    s_runs = s_measures = 0;
    (void)viper_scpi_init(&c, &cfg);
}

/* The last unit of a full line suspends; the message resumes after it and
 * does not run again from its start. */
static void test_resume_after_last_unit(void)
{
    setup();
    send_full("INIT;", ";*WAI");
    CHECK(viper_scpi_busy(&c));
    viper_scpi_op_end(&c);
    viper_scpi_poll(&c);
    CHECK(!viper_scpi_busy(&c));
    CHECK_INT(s_runs, 1);
    CHECK_INT(viper_scpi_error_count(&c), 0);

    setup();
    send_full("INIT;", ";*OPC?");
    viper_scpi_op_end(&c);
    viper_scpi_poll(&c);
    viper_scpi_poll(&c);
    CHECK(!viper_scpi_busy(&c));
    CHECK_INT(s_runs, 1);
    CHECK_STR(s_out, "1\n");

    setup();
    send_full("", ";MEAS?");
    CHECK(viper_scpi_busy(&c));
    viper_scpi_poll(&c);
    viper_scpi_poll(&c);
    CHECK(!viper_scpi_busy(&c));
    CHECK_INT(s_measures, 2);
    CHECK_STR(s_out, "42\n");
}

int main(void)
{
    test_resume_after_last_unit();
    return test_summary("test_bigbuf");
}
