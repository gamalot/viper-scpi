/* Minimal test helpers and shared fixture for the viper-scpi test suites. */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdio.h>
#include <string.h>

#include "viper_scpi/viper_scpi.h"

extern int g_checks;
extern int g_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failures++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                    \
    } while (0)

#define CHECK_INT(actual, expected)                                          \
    do {                                                                     \
        long a_ = (long)(actual);                                            \
        long e_ = (long)(expected);                                          \
        g_checks++;                                                          \
        if (a_ != e_) {                                                      \
            g_failures++;                                                    \
            printf("FAIL %s:%d: %s = %ld, expected %ld\n", __FILE__,         \
                   __LINE__, #actual, a_, e_);                               \
        }                                                                    \
    } while (0)

#define CHECK_STR(actual, expected)                                          \
    do {                                                                     \
        const char *a_ = (actual);                                           \
        const char *e_ = (expected);                                         \
        g_checks++;                                                          \
        if (strcmp(a_, e_) != 0) {                                           \
            g_failures++;                                                    \
            printf("FAIL %s:%d: got \"%s\", expected \"%s\"\n", __FILE__,    \
                   __LINE__, a_, e_);                                        \
        }                                                                    \
    } while (0)

/* Fixture: a context with a small command tree that records what it saw. */
extern viper_scpi_context_t g_ctx;
extern double g_volt;
extern double g_curr;
extern int g_output;
extern int g_mode;
extern int32_t g_count;
extern uint32_t g_ucount;
extern char g_name[32];
extern int g_reset_calls;
extern double g_limit;
extern int32_t g_pcount;
extern uint8_t g_block[32];
extern size_t g_block_len;

/** Re-initialise the context, tree state and output capture. */
void fx_setup(void);
/** Feed @p line followed by '\n'. */
void fx_send(const char *line);
/** Everything written since the last fx_clear(). */
const char *fx_out(void);
void fx_clear(void);
/** Print the summary and return the process exit code. */
int test_summary(const char *suite);

#endif /* TEST_UTIL_H */
