/* Error queue behaviour and the built-in SYSTem subtree. */
#include "test_util.h"

static void test_queue_fifo(void)
{
    fx_setup();
    CHECK_INT(viper_scpi_error_pop(&g_ctx), 0);
    viper_scpi_error_push(&g_ctx, -100);
    viper_scpi_error_push(&g_ctx, -200);
    viper_scpi_error_push(&g_ctx, 0);      /* zero is never queued */
    CHECK_INT(viper_scpi_error_count(&g_ctx), 2);
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -100);
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -200);
    CHECK_INT(viper_scpi_error_pop(&g_ctx), 0);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_queue_overflow(void)
{
    int i;

    fx_setup();
    for (i = 0; i < VIPER_SCPI_ERROR_QUEUE_SIZE + 3; i++) {
        viper_scpi_error_push(&g_ctx, -101 - i);
    }
    CHECK_INT(viper_scpi_error_count(&g_ctx), VIPER_SCPI_ERROR_QUEUE_SIZE);
    for (i = 0; i < VIPER_SCPI_ERROR_QUEUE_SIZE - 1; i++) {
        CHECK_INT(viper_scpi_error_pop(&g_ctx), -101 - i);
    }
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -350);   /* last slot replaced */
    CHECK_INT(viper_scpi_error_pop(&g_ctx), 0);
}

static void test_queue_wraparound(void)
{
    int i;

    fx_setup();
    for (i = 0; i < 3 * VIPER_SCPI_ERROR_QUEUE_SIZE; i++) {
        viper_scpi_error_push(&g_ctx, -101 - (i % 50));
        CHECK_INT(viper_scpi_error_pop(&g_ctx), -101 - (i % 50));
    }
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
}

static void test_clear(void)
{
    fx_setup();
    viper_scpi_error_push(&g_ctx, -100);
    viper_scpi_error_clear(&g_ctx);
    CHECK_INT(viper_scpi_error_count(&g_ctx), 0);
    CHECK_INT(viper_scpi_error_pop(&g_ctx), 0);
}

static void test_messages(void)
{
    CHECK_STR(viper_scpi_error_message(0), "No error");
    CHECK_STR(viper_scpi_error_message(-113), "Undefined header");
    CHECK_STR(viper_scpi_error_message(-350), "Queue overflow");
    CHECK_STR(viper_scpi_error_message(-999), "Device-specific error");
}

static void test_syst_err_query(void)
{
    fx_setup();
    fx_send("SYST:ERR?");
    CHECK_STR(fx_out(), "0,\"No error\"\n");
    fx_clear();

    fx_send("BOGUS");
    fx_send("SYSTEM:ERROR?");
    CHECK_STR(fx_out(), "-113,\"Undefined header\"\n");
    fx_clear();

    fx_send("SYST:ERR:NEXT?");
    CHECK_STR(fx_out(), "0,\"No error\"\n");
    fx_clear();

    fx_send("BOGUS;COUNT abc;SYST:ERR:COUN?");
    CHECK_STR(fx_out(), "2\n");
    fx_clear();
    fx_send("syst:err:next?;:syst:err?;:syst:err?");
    CHECK_STR(fx_out(), "-113,\"Undefined header\";-104,\"Data type error\";0,\"No error\"\n");
}

static void test_syst_relative_path(void)
{
    fx_setup();
    fx_send("BOGUS");
    fx_send("SYST:ERR:COUN?;NEXT?;COUN?");
    CHECK_STR(fx_out(), "1;-113,\"Undefined header\";0\n");
}

static void test_syst_version(void)
{
    fx_setup();
    fx_send("SYST:VERS?");
    CHECK_STR(fx_out(), "1999.0\n");
}

static void test_syst_misuse(void)
{
    fx_setup();
    fx_send("SYST:ERR");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("SYST:ERR:COUN");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("SYST:VERS");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("SYST:ERR? 1");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -108);
    fx_send("SYST:ERR:COUN? 1");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -108);
    fx_send("SYST:VERS? 1");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -108);
    fx_send("SYST?");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    fx_send("SYST:BOGUS?");
    CHECK_INT(viper_scpi_error_pop(&g_ctx), -113);
    CHECK_STR(fx_out(), "");
}

/* ---- Device-specific texts and detail ---------------------------------- */

static char s_dev_out[256];
static size_t s_dev_len;

static size_t dev_write(void *user, const char *data, size_t len)
{
    (void)user;
    if (s_dev_len + len < sizeof s_dev_out) {
        memcpy(s_dev_out + s_dev_len, data, len);
        s_dev_len += len;
        s_dev_out[s_dev_len] = '\0';
    }
    return len;
}

static const viper_scpi_error_text_t dev_texts[] = {
    { 101, "Relay stuck" },
    { -222, "Value outside the instrument's range" },  /* overrides a built-in */
    { -300, "Device \"hot\"" },
    { 0, NULL }
};

static const viper_scpi_config_t dev_cfg = {
    .write = dev_write,
    .error_texts = dev_texts,
};

static viper_scpi_context_t dev;

static const char *dev_ask(const char *q)
{
    s_dev_len = 0;
    s_dev_out[0] = '\0';
    viper_scpi_input_buffer(&dev, q, strlen(q));
    viper_scpi_input_char(&dev, '\n');
    return s_dev_out;
}

static void test_device_texts(void)
{
    (void)viper_scpi_init(&dev, &dev_cfg);
    viper_scpi_error_push(&dev, 101);
    viper_scpi_error_push(&dev, -222);
    viper_scpi_error_push(&dev, -113);                 /* not listed: built-in */
    viper_scpi_error_push(&dev, 102);                  /* unknown device code */
    CHECK_STR(dev_ask("SYST:ERR?"), "101,\"Relay stuck\"\n");
    CHECK_STR(dev_ask("SYST:ERR?"), "-222,\"Value outside the instrument's range\"\n");
    CHECK_STR(dev_ask("SYST:ERR?"), "-113,\"Undefined header\"\n");
    CHECK_STR(dev_ask("SYST:ERR?"), "102,\"Device-specific error\"\n");
    CHECK_STR(viper_scpi_error_text(&dev, 101), "Relay stuck");
    CHECK_STR(viper_scpi_error_text(&dev, 0), "No error");
    CHECK_STR(viper_scpi_error_message(101), "Device-specific error"); /* built-in only */

    /* Without a table the built-in texts are used. */
    fx_setup();
    CHECK_STR(viper_scpi_error_text(&g_ctx, -222), "Data out of range");
}

static void test_error_info(void)
{
    const char *info = "x";

    (void)viper_scpi_init(&dev, &dev_cfg);
    viper_scpi_error_push_info(&dev, -113, "VOLT 31 exceeds 30");
    viper_scpi_error_push_info(&dev, -300, "at \"T1\"");   /* quotes doubled */
    viper_scpi_error_push_info(&dev, 101, NULL);
    viper_scpi_error_push_info(&dev, 0, "ignored");
    CHECK_INT(viper_scpi_error_count(&dev), 3);
    CHECK_STR(dev_ask("SYST:ERR?"), "-113,\"Undefined header;VOLT 31 exceeds 30\"\n");
    CHECK_STR(dev_ask("SYST:ERR?"), "-300,\"Device \"\"hot\"\";at \"\"T1\"\"\"\n");
    CHECK_STR(dev_ask("SYST:ERR?"), "101,\"Relay stuck\"\n");

    viper_scpi_error_push_info(&dev, -200, "detail");
    CHECK_INT(viper_scpi_error_pop_info(&dev, &info), -200);
    CHECK_STR(info, "detail");
    CHECK_INT(viper_scpi_error_pop_info(&dev, &info), 0);
    CHECK(info == NULL);
    CHECK_INT(viper_scpi_error_pop_info(&dev, NULL), 0);

    /* Overflow: the last slot becomes -350 and loses its detail. */
    for (int i = 0; i < VIPER_SCPI_ERROR_QUEUE_SIZE + 2; i++) {
        viper_scpi_error_push_info(&dev, -200, "lost");
    }
    for (int i = 0; i < VIPER_SCPI_ERROR_QUEUE_SIZE - 1; i++) {
        CHECK_INT(viper_scpi_error_pop_info(&dev, &info), -200);
        CHECK_STR(info, "lost");
    }
    CHECK_STR(dev_ask("SYST:ERR?"), "-350,\"Queue overflow\"\n");

    /* *CLS empties the queue, details included. */
    viper_scpi_error_push_info(&dev, -200, "gone");
    dev_ask("*CLS");
    CHECK_STR(dev_ask("SYST:ERR?"), "0,\"No error\"\n");
}

int main(void)
{
    test_queue_fifo();
    test_queue_overflow();
    test_queue_wraparound();
    test_clear();
    test_messages();
    test_syst_err_query();
    test_syst_relative_path();
    test_syst_version();
    test_syst_misuse();
    test_device_texts();
    test_error_info();
    return test_summary("test_error");
}
