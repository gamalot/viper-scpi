/* Message-exchange model: output queue, MAV, -410, generated blocks,
 * device clear, service-request edges, *TRG. */
#include "test_util.h"

/* ---- A context with an output buffer, as USBTMC / VXI-11 would use ---- */

static char s_qbuf[64];
static viper_scpi_context_t q;
static int s_triggers;

/* byte[i] = i mod 256, so every byte can be checked without a reference */
static void fill_pattern(void *user, uint8_t *dst, uint32_t offset, size_t count)
{
    (void)user;
    for (size_t i = 0; i < count; i++) {
        dst[i] = (uint8_t)(offset + i);
    }
}

static viper_scpi_result_t cb_data(viper_scpi_context_t *ctx)
{
    int32_t n;
    viper_scpi_result_t res = viper_scpi_param_int32_kw(ctx, 0, 0, 1000000, 0, &n);

    if (res == VIPER_SCPI_RES_OK) {
        viper_scpi_result_block_fill(ctx, (uint32_t)n, fill_pattern, NULL);
    }
    return res;
}

static viper_scpi_result_t cb_two(viper_scpi_context_t *ctx)
{
    viper_scpi_result_block_fill(ctx, 3, fill_pattern, NULL);
    viper_scpi_result_block_fill(ctx, 3, fill_pattern, NULL);
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cb_big(viper_scpi_context_t *ctx)
{
    /* Longer than s_qbuf on purpose. */
    viper_scpi_result_text(ctx, "0123456789012345678901234567890123456789");
    viper_scpi_result_text(ctx, "0123456789012345678901234567890123456789");
    return VIPER_SCPI_RES_OK;
}

static void on_trigger(viper_scpi_context_t *ctx)
{
    (void)ctx;
    s_triggers++;
}

static const viper_scpi_node_t q_root[] = {
    { "DATA", cb_data, NULL },
    { "TWO", cb_two, NULL },
    { "BIG", cb_big, NULL },
    VIPER_SCPI_NODE_END
};

static size_t never_write(void *user, const char *data, size_t len)
{
    (void)user;
    (void)data;
    CHECK(0 && "write must not be used in the message-exchange model");
    return len;
}

static const viper_scpi_config_t q_cfg = {
    .root = q_root,
    .write = never_write,
    .idn_manufacturer = "Acme",
    .idn_model = "Q",
    .idn_serial = "1",
    .idn_firmware = "2",
    .output_buffer = s_qbuf,
    .output_size = sizeof s_qbuf,
    .trigger = on_trigger,
};

static void q_setup(void)
{
    s_triggers = 0;
    (void)viper_scpi_init(&q, &q_cfg);
    viper_scpi_input_buffer(&q, "*ESR?\n", 6);   /* consume PON */
    {
        uint8_t tmp[8];
        bool end;
        (void)viper_scpi_output_read(&q, tmp, sizeof tmp, &end);
    }
}

static void q_send(const char *s)
{
    viper_scpi_input_buffer(&q, s, strlen(s));
    viper_scpi_end_of_message(&q);
}

/* Read the whole pending response in chunks of @p chunk bytes. */
static size_t q_read_all(char *dst, size_t size, size_t chunk, int *reads)
{
    size_t total = 0;
    bool end = false;

    *reads = 0;
    while (!end && total < size) {
        size_t max = (size - total < chunk) ? size - total : chunk;
        total += viper_scpi_output_read(&q, (uint8_t *)dst + total, max, &end);
        (*reads)++;
    }
    if (total < size) {
        dst[total] = '\0';
    }
    return total;
}

/* ---- Tests ------------------------------------------------------------- */

static void test_queued_reply(void)
{
    char buf[64];
    int reads;
    size_t n;

    q_setup();
    CHECK(!viper_scpi_output_pending(&q));
    q_send("*IDN?");
    CHECK(viper_scpi_output_pending(&q));
    CHECK_INT(viper_scpi_status_byte(&q), VIPER_SCPI_STB_MAV);

    n = q_read_all(buf, sizeof buf, 5, &reads);   /* "Acme,Q,1,2\n" = 11 bytes */
    CHECK_INT(n, 11);
    CHECK_INT(reads, 3);
    CHECK_STR(buf, "Acme,Q,1,2\n");
    CHECK(!viper_scpi_output_pending(&q));
    CHECK_INT(viper_scpi_status_byte(&q), 0);

    /* Nothing queued: a read returns nothing and reports the end. */
    {
        bool end = false;
        CHECK_INT(viper_scpi_output_read(&q, (uint8_t *)buf, sizeof buf, &end), 0);
        CHECK(end);
    }
}

static void test_mav_in_stb_query(void)
{
    char buf[64];
    int reads;

    q_setup();
    q_send("*OPC?;*STB?");   /* MAV is set by the time *STB? runs */
    q_read_all(buf, sizeof buf, 64, &reads);
    CHECK_STR(buf, "1;16\n");
}

static void test_query_interrupted(void)
{
    char buf[64];
    int reads;

    q_setup();
    q_send("*IDN?");
    q_send("*OPC?");                /* reply to *IDN? never read */
    q_read_all(buf, sizeof buf, 64, &reads);
    CHECK_STR(buf, "1\n");
    CHECK_INT(viper_scpi_error_pop(&q), VIPER_SCPI_RES_ERR_QUERY_INTERRUPTED);
    CHECK_INT(viper_scpi_error_count(&q), 0);

    /* Partly read, then a new message: still interrupted. */
    q_send("*IDN?");
    {
        bool end;
        (void)viper_scpi_output_read(&q, (uint8_t *)buf, 3, &end);
        CHECK(!end);
    }
    q_send("*OPC");                 /* not *CLS: that would empty the queue */
    CHECK(!viper_scpi_output_pending(&q));
    CHECK_INT(viper_scpi_error_pop(&q), -410);
}

static void test_empty_line_is_not_a_message(void)
{
    char buf[64];
    int reads;

    q_setup();
    viper_scpi_input_buffer(&q, "*OPC?\n\n\r\n", 9);
    viper_scpi_end_of_message(&q);
    CHECK_INT(viper_scpi_error_count(&q), 0);
    q_read_all(buf, sizeof buf, 64, &reads);
    CHECK_STR(buf, "1\n");
}

static void test_generated_block(void)
{
    static char big[1200];
    int reads;
    size_t n;
    bool ok = true;

    q_setup();
    q_send("DATA? 1000");
    CHECK_INT(viper_scpi_status_byte(&q), VIPER_SCPI_STB_MAV);
    n = q_read_all(big, sizeof big, 64, &reads);
    CHECK_INT(n, 6 + 1000 + 1);                 /* "#41000" + data + "\n" */
    CHECK(memcmp(big, "#41000", 6) == 0);
    for (size_t i = 0; i < 1000; i++) {
        ok = ok && (uint8_t)big[6 + i] == (uint8_t)i;
    }
    CHECK(ok);
    CHECK(big[n - 1] == '\n');
    CHECK(!viper_scpi_output_pending(&q));

    /* Text before and after the block, in order. */
    q_send("*OPC?;DATA? 5;*OPC?");
    n = q_read_all(big, sizeof big, 3, &reads);
    CHECK_INT(n, 2 + 3 + 5 + 3);
    CHECK(memcmp(big, "1;#15\x00\x01\x02\x03\x04;1\n", n) == 0);

    /* Empty block. */
    q_send("DATA? 0");
    n = q_read_all(big, sizeof big, 64, &reads);
    CHECK_INT(n, 4);
    CHECK(memcmp(big, "#10\n", 4) == 0);

    /* Larger than the output buffer many times over: generated as read. */
    q_send("DATA? MAX");
    {
        size_t total = 0;
        bool end = false;
        uint8_t chunk[512];
        while (!end) {
            total += viper_scpi_output_read(&q, chunk, sizeof chunk, &end);
        }
        CHECK_INT(total, 9 + 1000000 + 1);
    }
    CHECK_INT(viper_scpi_error_count(&q), 0);
}

static void test_second_block_is_an_error(void)
{
    char buf[64];
    int reads;

    q_setup();
    q_send("TWO?");
    q_read_all(buf, sizeof buf, 64, &reads);
    CHECK(memcmp(buf, "#13\x00\x01\x02\n", 7) == 0);
    CHECK_INT(viper_scpi_error_pop(&q), VIPER_SCPI_RES_ERR_QUERY);
}

static void test_output_overflow_drops_whole_reply(void)
{
    q_setup();
    q_send("BIG?");
    CHECK(!viper_scpi_output_pending(&q));      /* no truncated reply */
    CHECK_INT(viper_scpi_error_pop(&q), VIPER_SCPI_RES_ERR_OUT_OF_MEMORY);
    CHECK_INT(viper_scpi_error_count(&q), 0);
    CHECK_STR(viper_scpi_error_message(-321), "Out of memory");

    /* The next message is unaffected. */
    q_send("*OPC?");
    CHECK(viper_scpi_output_pending(&q));
}

static void test_device_clear(void)
{
    char buf[64];
    int reads;

    q_setup();
    q_send("*ESE 1;*OPC;BOGUS");           /* ESR OPC+CME, one error queued */
    q_send("*IDN?");                       /* reply left unread */
    viper_scpi_input_buffer(&q, "*SRE 3", 6); /* half a message */

    viper_scpi_device_clear(&q);
    CHECK(!viper_scpi_output_pending(&q));

    q_send("*SRE?");                       /* the half message is gone */
    q_read_all(buf, sizeof buf, 64, &reads);
    CHECK_STR(buf, "0\n");
    CHECK_INT(viper_scpi_error_count(&q), 1); /* -113 kept, no -410: cleared, not interrupted */
    CHECK_INT(viper_scpi_error_pop(&q), -113);
    q_send("*ESR?");                       /* status registers kept */
    q_read_all(buf, sizeof buf, 64, &reads);
    CHECK_STR(buf, "33\n");
}

static void test_output_discard(void)
{
    q_setup();
    q_send("DATA? 100");
    viper_scpi_output_discard(&q);
    CHECK(!viper_scpi_output_pending(&q));
    q_send("*OPC?");
    CHECK_INT(viper_scpi_error_count(&q), 0); /* discarded, so no -410 */
}

static void test_query_unterminated_text(void)
{
    q_setup();
    viper_scpi_error_push(&q, VIPER_SCPI_RES_ERR_QUERY_UNTERMINATED);
    CHECK_STR(viper_scpi_error_message(-420), "Query UNTERMINATED");
    CHECK_STR(viper_scpi_error_message(-410), "Query INTERRUPTED");
    q_send("*ESR?");
    {
        char buf[16];
        int reads;
        q_read_all(buf, sizeof buf, 16, &reads);
        CHECK_STR(buf, "4\n");             /* QYE */
    }
}

static void test_service_request_edges(void)
{
    char buf[64];
    int reads;

    q_setup();
    CHECK(!viper_scpi_take_service_request(&q));

    /* The condition is true before and after the second line and false in
     * its middle: each line is still one new request. */
    q_send("*ESE 1;*SRE 32;*OPC");
    CHECK(viper_scpi_take_service_request(&q));
    CHECK(!viper_scpi_take_service_request(&q));   /* taken once */
    q_send("*CLS;*ESE 1;*SRE 32;*OPC");
    CHECK(viper_scpi_take_service_request(&q));
    CHECK(!viper_scpi_take_service_request(&q));

    /* Still true, no transition: no new request. */
    q_send("*OPC");
    CHECK(!viper_scpi_take_service_request(&q));

    /* MAV: each reply is a new request once the previous was read. */
    q_send("*CLS;*SRE 16");
    CHECK(!viper_scpi_take_service_request(&q));
    q_send("*OPC?");
    CHECK(viper_scpi_take_service_request(&q));
    q_read_all(buf, sizeof buf, 64, &reads);
    q_send("*OPC?");
    CHECK(viper_scpi_take_service_request(&q));
    q_read_all(buf, sizeof buf, 64, &reads);

    /* Errors raise EAV outside any command too (e.g. -420 from a transport). */
    q_send("*SRE 4");
    CHECK(!viper_scpi_take_service_request(&q));
    viper_scpi_error_push(&q, VIPER_SCPI_RES_ERR_QUERY_UNTERMINATED);
    CHECK(viper_scpi_take_service_request(&q));

    /* User events through viper_scpi_event_set(). */
    q_send("*CLS;*ESE 64;*SRE 32");
    CHECK(!viper_scpi_take_service_request(&q));
    viper_scpi_event_set(&q, VIPER_SCPI_ESR_URQ);
    CHECK(viper_scpi_take_service_request(&q));
}

static void test_trigger_hook(void)
{
    q_setup();
    q_send("*TRG;*trg");
    CHECK_INT(s_triggers, 2);
    CHECK_INT(viper_scpi_error_count(&q), 0);
    q_send("*TRG 1");
    CHECK_INT(viper_scpi_error_pop(&q), -108);
    CHECK_INT(s_triggers, 2);
}

/* ---- Stream model: generated blocks go straight to write ----------------- */

static void test_generated_block_stream_model(void)
{
    const char *out;
    bool ok = true;

    fx_setup();
    viper_scpi_result_block_fill(&g_ctx, 200, fill_pattern, NULL);
    out = fx_out();
    CHECK(memcmp(out, "#3200", 5) == 0);
    for (size_t i = 0; i < 200 && ok; i++) {
        /* fx_out() is NUL terminated, so a 0 byte ends strcmp-style views;
         * compare bytes directly instead. */
        ok = (uint8_t)out[5 + i] == (uint8_t)i;
    }
    CHECK(ok);
    CHECK(!viper_scpi_output_pending(&g_ctx));     /* nothing queued */
}

static void test_truncated_block_at_eom(void)
{
    char buf[64];
    int reads;

    q_setup();
    q_send("*IDN?");
    viper_scpi_input_buffer(&q, "DATA #15ab", 10);   /* EOM 3 bytes short */
    viper_scpi_end_of_message(&q);
    CHECK_INT(viper_scpi_error_pop(&q), -410);       /* it was still a message */
    CHECK_INT(viper_scpi_error_pop(&q), -161);
    CHECK(!viper_scpi_output_pending(&q));           /* nothing ran */
    q_send("*OPC?");                                 /* next message unaffected */
    q_read_all(buf, sizeof buf, 64, &reads);
    CHECK_STR(buf, "1\n");
    CHECK_INT(viper_scpi_error_count(&q), 0);
}

static void test_input_discard_keeps_reply(void)
{
    char buf[64];
    int reads;

    q_setup();
    q_send("*IDN?");
    viper_scpi_input_buffer(&q, "*RST;DATA #19abc", 16);  /* half a message */
    viper_scpi_input_discard(&q);
    CHECK(viper_scpi_output_pending(&q));                /* reply survives */
    CHECK_INT(viper_scpi_error_count(&q), 0);            /* no -410: no message */
    q_read_all(buf, sizeof buf, 64, &reads);
    CHECK_STR(buf, "Acme,Q,1,2\n");

    /* The half message is gone, including its block state: the next bytes
     * start a fresh message rather than being taken as block data. */
    q_send("*OPC?");
    q_read_all(buf, sizeof buf, 64, &reads);
    CHECK_STR(buf, "1\n");
    CHECK_INT(viper_scpi_error_count(&q), 0);

    /* Quote state is reset too. */
    viper_scpi_input_buffer(&q, "*IDN? \"unterminated", 19);
    viper_scpi_input_discard(&q);
    q_send("*OPC?");
    q_read_all(buf, sizeof buf, 64, &reads);
    CHECK_STR(buf, "1\n");

    /* Discarding with nothing buffered is harmless. */
    viper_scpi_input_discard(&q);
    CHECK(!viper_scpi_output_pending(&q));
    CHECK_INT(viper_scpi_error_count(&q), 0);
}

static void test_serial_poll_rqs(void)
{
    char buf[64];
    int reads;

    q_setup();
    /* No request: bit 6 clear in the poll, nothing pending. */
    CHECK_INT(viper_scpi_serial_poll(&q), 0);

    /* MAV-driven request. The condition alone is not RQS: only a request
     * the transport took (and so signalled) is. */
    q_send("*SRE 16;*OPC?");
    CHECK_INT(viper_scpi_serial_poll(&q), VIPER_SCPI_STB_MAV);
    q_read_all(buf, sizeof buf, 64, &reads);
    q_send("*OPC?");
    CHECK(viper_scpi_take_service_request(&q));
    CHECK_INT(viper_scpi_serial_poll(&q), VIPER_SCPI_STB_MAV | VIPER_SCPI_STB_RQS);
    CHECK_INT(viper_scpi_serial_poll(&q), VIPER_SCPI_STB_MAV);     /* cleared */

    /* *STB? reports MSS and clears nothing. */
    q_read_all(buf, sizeof buf, 64, &reads);
    q_send("*SRE 32;*ESE 1;*OPC");
    CHECK(viper_scpi_take_service_request(&q));
    q_send("*STB?");
    q_read_all(buf, sizeof buf, 64, &reads);
    CHECK_STR(buf, "96\n");                                      /* ESB + MSS */
    q_send("*STB?");
    q_read_all(buf, sizeof buf, 64, &reads);
    CHECK_STR(buf, "96\n");
    CHECK_INT(viper_scpi_serial_poll(&q), VIPER_SCPI_STB_ESB | VIPER_SCPI_STB_RQS);
    CHECK_INT(viper_scpi_serial_poll(&q), VIPER_SCPI_STB_ESB);

    /* rqs_delivered() clears it, for SRQ messages carrying the byte. */
    q_send("*CLS;*ESE 1;*OPC");
    CHECK(viper_scpi_take_service_request(&q));
    viper_scpi_rqs_delivered(&q);
    CHECK_INT(viper_scpi_serial_poll(&q), VIPER_SCPI_STB_ESB);

    /* A new edge sets it again. */
    q_send("*CLS;*ESE 1;*OPC");
    CHECK(viper_scpi_take_service_request(&q));
    CHECK_INT(viper_scpi_serial_poll(&q), VIPER_SCPI_STB_ESB | VIPER_SCPI_STB_RQS);

    /* A request whose reason vanishes before the poll is withdrawn. */
    q_send("*CLS;*ESE 1;*OPC");
    CHECK(viper_scpi_take_service_request(&q));
    q_send("*CLS");
    CHECK_INT(viper_scpi_serial_poll(&q), 0);
}

int main(void)
{
    test_queued_reply();
    test_mav_in_stb_query();
    test_query_interrupted();
    test_empty_line_is_not_a_message();
    test_generated_block();
    test_second_block_is_an_error();
    test_output_overflow_drops_whole_reply();
    test_device_clear();
    test_output_discard();
    test_query_unterminated_text();
    test_service_request_edges();
    test_trigger_hook();
    test_generated_block_stream_model();
    test_truncated_block_at_eom();
    test_input_discard_keeps_reply();
    test_serial_poll_rqs();
    return test_summary("test_message");
}
