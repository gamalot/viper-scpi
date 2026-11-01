/* Overlapped commands: pending operations, *OPC, *OPC?, *WAI, viper_scpi_poll. */
#include "test_util.h"

static char s_buf[128];
static viper_scpi_context_t q;
static int s_count_calls;

/* Starts an operation and returns at once, like a slow measurement. */
static viper_scpi_result_t cb_start(viper_scpi_context_t *ctx)
{
    viper_scpi_op_begin(ctx);
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cb_count(viper_scpi_context_t *ctx)
{
    s_count_calls++;
    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_int(ctx, s_count_calls);
    }
    return VIPER_SCPI_RES_OK;
}

static const viper_scpi_node_t root[] = {
    { "STARt", cb_start, NULL },
    { "COUNt", cb_count, NULL },
    VIPER_SCPI_NODE_END
};

static const viper_scpi_config_t cfg = {
    .root = root,
    .idn_manufacturer = "Acme",
    .idn_model = "OV",
    .idn_serial = "1",
    .idn_firmware = "2",
    .output_buffer = s_buf,
    .output_size = sizeof s_buf,
};

static void send(const char *s)
{
    viper_scpi_input_buffer(&q, s, strlen(s));
    viper_scpi_end_of_message(&q);
}

/* Whole pending reply as a string ("" if none is readable). */
static const char *reply(void)
{
    static char out[128];
    size_t n = 0;
    bool end = false;

    while (!end && n < sizeof out - 1) {
        size_t k = viper_scpi_output_read(&q, (uint8_t *)out + n, sizeof out - 1 - n, &end);
        n += k;
        if (k == 0) {
            break;
        }
    }
    out[n] = '\0';
    return out;
}

static void setup(void)
{
    s_count_calls = 0;
    (void)viper_scpi_init(&q, &cfg);
    send("*ESR?");
    (void)reply();
}

static int esr(void)
{
    int v = -1;
    send("*ESR?");
    (void)sscanf(reply(), "%d", &v);
    return v;
}

static void test_no_operation_is_immediate(void)
{
    setup();
    CHECK(!viper_scpi_op_pending(&q));
    send("*OPC?;*WAI;*OPC");
    CHECK(!viper_scpi_busy(&q));
    CHECK_STR(reply(), "1\n");
    CHECK_INT(esr(), VIPER_SCPI_ESR_OPC);
    viper_scpi_op_end(&q);                 /* nothing pending: ignored */
    CHECK(!viper_scpi_op_pending(&q));
}

static void test_opc_waits_for_completion(void)
{
    setup();
    send("*ESE 1;*SRE 32;STAR;*OPC");
    CHECK(viper_scpi_op_pending(&q));
    CHECK(!viper_scpi_busy(&q));           /* *OPC does not suspend */
    CHECK(!viper_scpi_take_service_request(&q));
    send("*ESR?");
    CHECK_STR(reply(), "0\n");             /* not yet */
    viper_scpi_op_end(&q);
    CHECK(viper_scpi_take_service_request(&q));   /* completion raises SRQ */
    CHECK_INT(esr(), VIPER_SCPI_ESR_OPC);
}

static void test_nested_operations(void)
{
    setup();
    send("STAR;STAR;*OPC");
    viper_scpi_op_end(&q);
    CHECK(viper_scpi_op_pending(&q));
    CHECK_INT(esr(), 0);
    viper_scpi_op_end(&q);
    CHECK_INT(esr(), VIPER_SCPI_ESR_OPC);
}

static void test_opc_query_waits(void)
{
    setup();
    send("STAR;*OPC?");
    CHECK(viper_scpi_busy(&q));
    CHECK(!viper_scpi_output_pending(&q)); /* MAV clear: no reply yet */
    CHECK_INT(viper_scpi_status_byte(&q) & VIPER_SCPI_STB_MAV, 0);
    CHECK_STR(reply(), "");                /* a read finds nothing */
    viper_scpi_poll(&q);                   /* still pending: no effect */
    CHECK(viper_scpi_busy(&q));
    viper_scpi_op_end(&q);                 /* finishes during *OPC? */
    CHECK(viper_scpi_busy(&q));            /* until the main loop polls */
    viper_scpi_poll(&q);
    CHECK(!viper_scpi_busy(&q));
    CHECK(viper_scpi_output_pending(&q));
    CHECK_STR(reply(), "1\n");
}

static void test_wai_in_middle_of_line(void)
{
    setup();
    send("COUN?;STAR;*WAI;COUN?;*OPC?");
    CHECK(viper_scpi_busy(&q));
    CHECK_INT(s_count_calls, 1);           /* units after *WAI have not run */
    CHECK(!viper_scpi_output_pending(&q)); /* first reply is held back */
    viper_scpi_op_end(&q);
    viper_scpi_poll(&q);
    CHECK_INT(s_count_calls, 2);
    CHECK_STR(reply(), "1;2;1\n");
    CHECK_INT(viper_scpi_error_count(&q), 0);
}

static void test_opc_query_then_more_units(void)
{
    setup();
    send("STAR;*OPC?;COUN?");
    CHECK_INT(s_count_calls, 0);
    viper_scpi_op_end(&q);
    viper_scpi_poll(&q);
    CHECK_STR(reply(), "1;1\n");
}

static void test_suspended_twice(void)
{
    setup();
    send("STAR;*WAI;STAR;*OPC?;COUN?");
    viper_scpi_op_end(&q);
    viper_scpi_poll(&q);                   /* runs up to the second wait */
    CHECK(viper_scpi_busy(&q));
    CHECK_INT(s_count_calls, 0);
    viper_scpi_op_end(&q);
    viper_scpi_poll(&q);
    CHECK(!viper_scpi_busy(&q));
    CHECK_STR(reply(), "1;1\n");
}

static void test_input_refused_while_busy(void)
{
    setup();
    send("STAR;*WAI;COUN");
    send("COUN;COUN");                     /* ignored transport mistake */
    CHECK_INT(viper_scpi_error_pop(&q), -363);
    viper_scpi_input_buffer(&q, "COUN", 4);
    viper_scpi_input_discard(&q);          /* forgets the refused bytes */
    viper_scpi_end_of_message(&q);
    CHECK_INT(viper_scpi_error_count(&q), 0);
    viper_scpi_op_end(&q);
    viper_scpi_poll(&q);
    CHECK_INT(s_count_calls, 1);           /* only the original COUN ran */
    send("COUN?");                         /* accepted again */
    CHECK_STR(reply(), "2\n");
}

static void test_device_clear_cancels(void)
{
    setup();
    send("STAR;*WAI;COUN");
    viper_scpi_device_clear(&q);
    CHECK(!viper_scpi_busy(&q));
    viper_scpi_op_end(&q);
    viper_scpi_poll(&q);
    CHECK_INT(s_count_calls, 0);           /* the rest never runs */
    CHECK_STR(reply(), "");

    send("STAR;*OPC");                     /* armed *OPC is cancelled too */
    viper_scpi_device_clear(&q);
    viper_scpi_op_end(&q);
    CHECK_INT(esr(), 0);

    send("STAR;*OPC?");                    /* and a waiting *OPC? */
    viper_scpi_device_clear(&q);
    viper_scpi_op_end(&q);
    viper_scpi_poll(&q);
    CHECK_STR(reply(), "");
    send("*OPC?");
    CHECK_STR(reply(), "1\n");
}

static void test_cls_and_rst_disarm_opc(void)
{
    setup();
    send("STAR;*OPC;*CLS");
    viper_scpi_op_end(&q);
    CHECK_INT(esr(), 0);
    send("STAR;*OPC;*RST");
    viper_scpi_op_end(&q);
    CHECK_INT(esr(), 0);
}

static void test_stream_model(void)
{
    fx_setup();
    /* The fixture has no operation-starting command, so begin one directly. */
    viper_scpi_op_begin(&g_ctx);
    fx_send("COUNT 5;*WAI;COUNT 6;COUNT?");
    CHECK_INT(g_count, 5);
    CHECK(viper_scpi_busy(&g_ctx));
    CHECK_STR(fx_out(), "");
    viper_scpi_op_end(&g_ctx);
    viper_scpi_poll(&g_ctx);
    CHECK_INT(g_count, 6);
    CHECK_STR(fx_out(), "6\n");
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

int main(void)
{
    test_no_operation_is_immediate();
    test_opc_waits_for_completion();
    test_nested_operations();
    test_opc_query_waits();
    test_wai_in_middle_of_line();
    test_opc_query_then_more_units();
    test_suspended_twice();
    test_input_refused_while_busy();
    test_device_clear_cancels();
    test_cls_and_rst_disarm_opc();
    test_stream_model();
    return test_summary("test_overlapped");
}
