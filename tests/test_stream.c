/* Streaming block input: blocks far larger than the line buffer. */
#include <stdlib.h>

#include "test_util.h"

static char s_out[128];
static viper_scpi_context_t q;

/* What the handler saw. */
static int s_begin, s_end, s_abort, s_data_calls;
static uint32_t s_declared, s_received;
static int s_bad_bytes;
static char s_name[16];
static size_t s_params_at_begin;
static int s_fail_at;          /* 1: fail BEGIN, 2: fail at first DATA */
static int s_plain_calls;      /* the node's own callback */

static uint8_t pattern_byte(uint32_t i)
{
    return (uint8_t)(i * 7u + 3u);
}

static viper_scpi_result_t up_stream(viper_scpi_context_t *ctx, viper_scpi_stream_event_t ev,
                                     const uint8_t *data, size_t len)
{
    switch (ev) {
    case VIPER_SCPI_STREAM_BEGIN:
        s_begin++;
        s_declared = (uint32_t)len;
        s_received = 0;
        s_params_at_begin = viper_scpi_param_count(ctx);
        s_name[0] = '\0';
        if (s_params_at_begin > 0) {
            (void)viper_scpi_param_string(ctx, 0, s_name, sizeof s_name, NULL);
        }
        return s_fail_at == 1 ? VIPER_SCPI_RES_ERR_EXECUTION : VIPER_SCPI_RES_OK;
    case VIPER_SCPI_STREAM_DATA:
        s_data_calls++;
        if (s_fail_at == 2) {
            return VIPER_SCPI_RES_ERR_OUT_OF_MEMORY;
        }
        for (size_t i = 0; i < len; i++) {
            if (data[i] != pattern_byte(s_received + (uint32_t)i)) {
                s_bad_bytes++;
            }
        }
        s_received += (uint32_t)len;
        return VIPER_SCPI_RES_OK;
    case VIPER_SCPI_STREAM_END:
        s_end++;
        viper_scpi_result_uint(ctx, s_received);   /* responses are allowed */
        return VIPER_SCPI_RES_OK;
    default:
        s_abort++;
        return VIPER_SCPI_RES_OK;
    }
}

static viper_scpi_result_t up_plain(viper_scpi_context_t *ctx)
{
    (void)ctx;
    s_plain_calls++;
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t store_plain(viper_scpi_context_t *ctx)
{
    const uint8_t *d;
    size_t n;
    return viper_scpi_param_block(ctx, 0, &d, &n);
}

static const viper_scpi_node_t mmem_children[] = {
    { "UPLoad", up_plain, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t root[] = {
    { "MMEMory", NULL, mmem_children },
    { "STORe", store_plain, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_stream_node_t streams[] = {
    { &mmem_children[0], up_stream },
    { NULL, NULL }
};

static const viper_scpi_config_t cfg = {
    .root = root,
    .idn_manufacturer = "Acme",
    .idn_model = "ST",
    .idn_serial = "1",
    .idn_firmware = "2",
    .output_buffer = s_out,
    .output_size = sizeof s_out,
    .stream_nodes = streams,
};

static void setup(void)
{
    s_begin = s_end = s_abort = s_data_calls = 0;
    s_declared = s_received = 0;
    s_bad_bytes = 0;
    s_fail_at = 0;
    s_plain_calls = 0;
    (void)viper_scpi_init(&q, &cfg);
}

static const char *reply(void)
{
    static char buf[128];
    size_t n = 0;
    bool end = false;

    while (!end) {
        size_t k = viper_scpi_output_read(&q, (uint8_t *)buf + n, sizeof buf - 1 - n, &end);
        n += k;
        if (k == 0) {
            break;
        }
    }
    buf[n] = '\0';
    return buf;
}

/* Sends "<prefix>#<n><len>", then len pattern bytes in random-sized chunks,
 * then <suffix>, without the EOM. */
static void send_block(const char *prefix, uint32_t len, uint32_t send_len, const char *suffix)
{
    char hdr[24];
    uint8_t chunk[97];
    uint32_t off = 0;
    int digits = snprintf(hdr, sizeof hdr, "%lu", (unsigned long)len);

    viper_scpi_input_buffer(&q, prefix, strlen(prefix));
    (void)snprintf(hdr, sizeof hdr, "#%d%lu", digits, (unsigned long)len);
    viper_scpi_input_buffer(&q, hdr, strlen(hdr));
    while (off < send_len) {
        uint32_t k = 1u + (uint32_t)rand() % sizeof chunk;
        if (k > send_len - off) {
            k = send_len - off;
        }
        for (uint32_t i = 0; i < k; i++) {
            chunk[i] = pattern_byte(off + i);
        }
        if (rand() % 4 == 0) {   /* sometimes byte by byte */
            for (uint32_t i = 0; i < k; i++) {
                viper_scpi_input_char(&q, (char)chunk[i]);
            }
        } else {
            viper_scpi_input_buffer(&q, (const char *)chunk, k);
        }
        off += k;
    }
    viper_scpi_input_buffer(&q, suffix, strlen(suffix));
}

static void test_large_upload(void)
{
    const uint32_t len = 40u * VIPER_SCPI_BUFFER_SIZE + 17u;   /* ~5 KB */

    setup();
    srand(7);
    send_block("MMEM:UPL 'wave.bin',", len, len, "");
    CHECK_INT(s_begin, 1);
    CHECK_INT(s_declared, len);
    CHECK_INT(s_params_at_begin, 1);
    CHECK_STR(s_name, "wave.bin");
    CHECK_INT(s_end, 0);                       /* the unit is not over yet */
    viper_scpi_end_of_message(&q);
    CHECK_INT(s_end, 1);
    CHECK_INT(s_received, len);
    CHECK_INT(s_bad_bytes, 0);
    CHECK(s_data_calls > 1);
    CHECK_INT(s_abort, 0);
    CHECK_INT(s_plain_calls, 0);
    CHECK_STR(reply(), "5137\n");
    CHECK_INT(viper_scpi_error_count(&q), 0);
}

static void test_following_units_and_lf(void)
{
    setup();
    srand(11);
    /* Units after the streamed one run. */
    send_block("MMEM:UPL ", 300, 300, " ;*OPC?;:MMEM:UPL #10\n");
    CHECK_INT(s_end, 1);
    CHECK_INT(s_plain_calls, 1);               /* only a first command streams */
    CHECK_STR(reply(), "300;1\n");
    CHECK_INT(viper_scpi_error_count(&q), 0);
    /* A relative header after the streamed unit inherits its path. */
    send_block("MMEM:UPL ", 5, 5, ";UPL\n");
    CHECK_INT(s_end, 2);
    CHECK_INT(s_plain_calls, 2);               /* MMEM:UPL */
    CHECK_INT(viper_scpi_error_count(&q), 0);
    CHECK_STR(reply(), "5\n");
}

static void test_eom_before_declared_length(void)
{
    setup();
    srand(3);
    send_block("MMEM:UPL ", 1000, 600, "");
    viper_scpi_end_of_message(&q);
    CHECK_INT(s_abort, 1);
    CHECK_INT(s_end, 0);
    CHECK_INT(viper_scpi_error_pop(&q), -161);
    CHECK_STR(reply(), "");
    viper_scpi_input_buffer(&q, "*OPC?", 5);   /* next message unaffected */
    viper_scpi_end_of_message(&q);
    CHECK_STR(reply(), "1\n");
    CHECK_INT(viper_scpi_error_count(&q), 0);
}

static void test_device_clear_mid_block(void)
{
    setup();
    srand(5);
    send_block("MMEM:UPL ", 1000, 400, "");
    viper_scpi_device_clear(&q);
    CHECK_INT(s_abort, 1);
    CHECK_INT(viper_scpi_error_count(&q), 0);
    viper_scpi_input_buffer(&q, "*OPC?", 5);   /* the rest is not block data */
    viper_scpi_end_of_message(&q);
    CHECK_STR(reply(), "1\n");

    /* input_discard() aborts as well. */
    send_block("MMEM:UPL ", 1000, 10, "");
    viper_scpi_input_discard(&q);
    CHECK_INT(s_abort, 2);
}

static void test_parameter_after_block(void)
{
    setup();
    send_block("MMEM:UPL ", 3, 3, ",5;*OPC?");
    viper_scpi_end_of_message(&q);
    CHECK_INT(s_abort, 1);
    CHECK_INT(s_end, 0);
    CHECK_INT(viper_scpi_error_pop(&q), -108);
    CHECK_STR(reply(), "1\n");                 /* later units still run */
}

static void test_handler_errors(void)
{
    setup();
    s_fail_at = 1;
    send_block("MMEM:UPL ", 500, 500, "");
    viper_scpi_end_of_message(&q);
    CHECK_INT(viper_scpi_error_pop(&q), -200);
    CHECK_INT(s_data_calls, 0);                /* data skipped */
    CHECK_INT(s_end, 0);
    CHECK_INT(viper_scpi_error_count(&q), 0);

    setup();
    s_fail_at = 2;
    send_block("MMEM:UPL ", 500, 500, ";*OPC?");
    viper_scpi_end_of_message(&q);
    CHECK_INT(viper_scpi_error_pop(&q), -321);
    CHECK_INT(s_data_calls, 1);                /* not called again */
    CHECK_INT(s_end, 0);
    CHECK_STR(reply(), "1\n");
}

static void test_non_streaming_node_unchanged(void)
{
    setup();
    send_block("STOR ", 1000, 1000, "");
    viper_scpi_end_of_message(&q);
    CHECK_INT(viper_scpi_error_pop(&q), -363);
    CHECK_INT(s_begin, 0);

    /* A streaming node that is not the first command is not streamed. */
    send_block("*CLS;MMEM:UPL ", 1000, 1000, "");
    viper_scpi_end_of_message(&q);
    CHECK_INT(viper_scpi_error_pop(&q), -363);
    CHECK_INT(s_begin, 0);

    /* Small indefinite blocks go to the node's own callback. */
    viper_scpi_input_buffer(&q, "MMEM:UPL #0abc", 14);
    viper_scpi_end_of_message(&q);
    CHECK_INT(s_plain_calls, 1);
    CHECK_INT(s_begin, 0);
    CHECK_INT(viper_scpi_error_count(&q), 0);
}

static void test_reply_hidden_and_interrupted(void)
{
    setup();
    viper_scpi_input_buffer(&q, "*OPC?", 5);
    viper_scpi_end_of_message(&q);
    CHECK(viper_scpi_output_pending(&q));
    send_block("MMEM:UPL ", 50, 50, ";");      /* a new message starts */
    CHECK_INT(viper_scpi_error_pop(&q), -410);
    CHECK(!viper_scpi_output_pending(&q));     /* END's "50" waits for EOM */
    viper_scpi_end_of_message(&q);
    CHECK_STR(reply(), "50\n");
}

/* The rest of a command after a streamed block is skipped with strings and
 * blocks recognised, so a ';' inside them does not start a command. */
static void test_tail_is_lexed(void)
{
    setup();
    send_block("MMEM:UPL ", 3, 3, ",'a;*OPC?;'");
    viper_scpi_end_of_message(&q);
    CHECK_INT(s_abort, 1);
    CHECK_INT(viper_scpi_error_pop(&q), -108);
    CHECK_INT(viper_scpi_error_count(&q), 0);
    CHECK_STR(reply(), "");

    setup();
    send_block("MMEM:UPL ", 3, 3, ",#14;*OP;*OPC?");
    viper_scpi_end_of_message(&q);
    CHECK_INT(viper_scpi_error_pop(&q), -108);
    CHECK_INT(viper_scpi_error_count(&q), 0);
    CHECK_STR(reply(), "1\n");                 /* the unit after it runs */
}

/* White space after a streamed block, NUL included, is not a parameter. */
static void test_tail_white_space(void)
{
    setup();
    send_block("MMEM:UPL ", 3, 3, "");
    viper_scpi_input_char(&q, '\0');
    viper_scpi_input_char(&q, '\r');
    viper_scpi_end_of_message(&q);
    CHECK_INT(s_end, 1);
    CHECK_INT(viper_scpi_error_count(&q), 0);
    CHECK_STR(reply(), "3\n");
}

/* The streamed command's header is the path for relative headers after it,
 * as for any other command (SCPI-99 6.2.4). */
static void test_path_after_streamed_unit(void)
{
    setup();
    send_block("MMEM:UPL ", 3, 3, ";UPL #13abc");
    viper_scpi_end_of_message(&q);
    CHECK_INT(s_end, 1);
    CHECK_INT(s_plain_calls, 1);               /* MMEM:UPL, not -113 */
    CHECK_INT(viper_scpi_error_count(&q), 0);
}

/* A message dropped after its streamed first command ran still ends that
 * command's reply properly. */
static void test_reply_terminated_when_rest_dropped(void)
{
    setup();
    send_block("MMEM:UPL ", 3, 3, ";STOR #15ab");
    viper_scpi_end_of_message(&q);
    CHECK_INT(viper_scpi_error_pop(&q), -161);
    CHECK_STR(reply(), "3\n");

    setup();
    send_block("MMEM:UPL ", 3, 3, ";*OPC");
    viper_scpi_input_discard(&q);
    CHECK_STR(reply(), "3\n");
}

int main(void)
{
    test_large_upload();
    test_following_units_and_lf();
    test_eom_before_declared_length();
    test_device_clear_mid_block();
    test_parameter_after_block();
    test_handler_errors();
    test_non_streaming_node_unchanged();
    test_reply_hidden_and_interrupted();
    test_tail_is_lexed();
    test_tail_white_space();
    test_path_after_streamed_unit();
    test_reply_terminated_when_rest_dropped();
    return test_summary("test_stream");
}
