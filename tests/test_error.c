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
    return test_summary("test_error");
}
