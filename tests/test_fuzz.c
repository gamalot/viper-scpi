/*
 * Deterministic fuzz smoke test, most useful under ASan/UBSan.
 * Part 1 (stream model): random bytes drawn from a protocol-heavy alphabet
 * are fed to a tree that exercises optional nodes, suffixes, blocks,
 * keywords and units; nothing may crash.
 * Part 2 (message-exchange model, with and without the leniency options):
 * random interleavings of input, EOM, reads of random size, discards,
 * device clear, overlapped operations, serial polls and status updates,
 * checking that reads never exceed the request, that a finished reply
 * leaves the queue empty with MAV clear, and that MAV always equals
 * viper_scpi_output_pending(). Coverage counters make sure the
 * interesting states are actually reached.
 */
#include <stdio.h>
#include <string.h>

#include "viper_scpi/viper_scpi.h"

static size_t sink(void *user_data, const char *data, size_t len)
{
    (void)user_data;
    (void)data;
    return len;
}

static viper_scpi_result_t cb(viper_scpi_context_t *ctx)
{
    const uint8_t *blk;
    size_t n;
    double d;
    int32_t i;
    uint32_t u;
    bool b;
    char s[16];
    static const viper_scpi_choice_t ch[] = { { "ONE", 1 }, { "TWO", 2 }, { NULL, 0 } };
    static const viper_scpi_number_t unit_less = { 0.0, 1.0, 0.5, NULL, 0 };
    static const viper_scpi_number_t hertz = { 0.0, 1e9, 1.0, viper_scpi_units_hertz,
                                               VIPER_SCPI_NUM_UPDOWN };
    static const viper_scpi_number_t seconds = { 0.0, 1.0, 0.5, viper_scpi_units_second, 0 };
    static const viper_scpi_integer_t digit = { 0, 9, 1, 0, 0, NULL };
    static const viper_scpi_integer_t millivolts = { -30000, 30000, 0, -3, VIPER_SCPI_NUM_UPDOWN,
                                                     viper_scpi_units_volt };
    viper_scpi_numeric_kw_t kw;
    int c;

    (void)viper_scpi_param_block(ctx, 0, &blk, &n);
    (void)viper_scpi_param_double(ctx, 0, &d);
    (void)viper_scpi_param_number(ctx, 1, &unit_less, &d, NULL);
    (void)viper_scpi_param_number(ctx, 0, &hertz, &d, &kw);
    (void)viper_scpi_param_number(ctx, 1, &seconds, &d, NULL);
    (void)viper_scpi_param_int32(ctx, 0, &i);
    (void)viper_scpi_param_integer(ctx, 1, &digit, &i, NULL);
    (void)viper_scpi_param_integer(ctx, 0, &millivolts, &i, &kw);
    (void)viper_scpi_param_uint32(ctx, 2, &u);
    (void)viper_scpi_param_bool(ctx, 0, &b);
    (void)viper_scpi_param_choice(ctx, 0, ch, &c);
    (void)viper_scpi_param_string(ctx, 0, s, sizeof s, &n);
    viper_scpi_result_int(ctx, viper_scpi_suffix(ctx, 0, 1));
    viper_scpi_result_string(ctx, "a\"b");
    viper_scpi_result_block(ctx, "xyz", 3);
    return VIPER_SCPI_RES_OK;
}

static const viper_scpi_node_t leaf[] = {
    { "X", cb, NULL },
    { "Y[:Z][:W#]:Q", cb, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t root[] = {
    { "[SENSe]", NULL, leaf },
    { "CHANnel#[:AB]", cb, leaf },
    { "A[:B", cb, NULL },      /* malformed on purpose */
    { "[]", cb, leaf },        /* malformed on purpose */
    { "A", NULL, leaf },
    { "[:A][:B]", cb, leaf },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_config_t config = { .root = root, .write = sink };

static uint32_t rng_state = 2463534242u;

static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* ---- Message-exchange model -------------------------------------------- */

static int s_failures;
/* Coverage counters: each must be non-zero, or the fuzz is not reaching
 * the state it is meant to exercise. */
static unsigned long s_busy, s_bytes_read, s_stream_begins, s_reply_ends;

#define FUZZ_ASSERT(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            if (s_failures++ < 10) {                                          \
                printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            }                                                                 \
        }                                                                     \
    } while (0)

static void fill(void *user, uint8_t *dst, uint32_t offset, size_t count)
{
    (void)user;
    for (size_t i = 0; i < count; i++) {
        dst[i] = (uint8_t)(offset + i);
    }
}

/* Generates a large block reply, starts operations, raises status bits. */
static viper_scpi_result_t cb_msg(viper_scpi_context_t *ctx)
{
    int32_t n = 0;

    (void)viper_scpi_param_int32(ctx, 0, &n);
    switch ((uint32_t)n % 5u) {
    case 0: viper_scpi_result_block_fill(ctx, (uint32_t)n % 3000u, fill, NULL); break;
    case 1: viper_scpi_op_begin(ctx); break;
    case 2: viper_scpi_status_condition(ctx, VIPER_SCPI_STATUS_QUES, 0x0202, (uint16_t)n); break;
    case 3: viper_scpi_error_push_info(ctx, 101, "fuzz \"detail\""); break;
    default: viper_scpi_result_text(ctx, "0123456789012345678901234567890123456789"); break;
    }
    return cb(ctx);
}

static viper_scpi_result_t stream_cb(viper_scpi_context_t *ctx, viper_scpi_stream_event_t ev,
                                     const uint8_t *data, size_t len)
{
    (void)data;
    if (ev == VIPER_SCPI_STREAM_BEGIN) {
        s_stream_begins++;
    }
    if (ev == VIPER_SCPI_STREAM_END) {
        viper_scpi_result_uint(ctx, (uint32_t)len);
    }
    return (len % 97u == 13u) ? VIPER_SCPI_RES_ERR_EXECUTION : VIPER_SCPI_RES_OK;
}

static const viper_scpi_node_t msg_root[] = {
    { "M", cb_msg, NULL },
    { "UPLoad", cb, NULL },
    { "CHANnel#[:AB]", cb, leaf },
    { "[SENSe]", NULL, leaf },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_stream_node_t msg_streams[] = {
    { &msg_root[1], stream_cb },
    { NULL, NULL }
};
static char s_out[96];
static void on_trg(viper_scpi_context_t *ctx)
{
    (void)ctx;
}

static const viper_scpi_config_t msg_config = {
    .root = msg_root, .trigger = on_trg, .output_buffer = s_out, .output_size = sizeof s_out,
    .stream_nodes = msg_streams,
};
static const viper_scpi_config_t msg_config_eom = {
    .root = msg_root, .output_buffer = s_out, .output_size = sizeof s_out,
    .stream_nodes = msg_streams,
    .options = VIPER_SCPI_OPT_EOM_MESSAGES | VIPER_SCPI_OPT_RELATIVE_FALLBACK,
};

static void check_mav(const viper_scpi_context_t *ctx)
{
    bool mav = (viper_scpi_status_byte(ctx) & VIPER_SCPI_STB_MAV) != 0;
    FUZZ_ASSERT(mav == viper_scpi_output_pending(ctx));
}

/* Appends a random program-message fragment: mostly valid commands, so the
 * interesting states (replies, suspension, streaming) are reached, with
 * noise mixed in. */
static size_t make_message(char *buf, size_t size)
{
    static const char *const pieces[] = {
        "*IDN?", "*OPC?", "*OPC", "*WAI", "*CLS", "*ESR?", "*STB?", "*RST", "*TRG",
        "SYST:ERR?", "SYST:ERR:COUN?", "STAT:QUES:ENAB 2", "STAT:QUES?", "*SRE 8",
        "*SRE 16", "*ESE 1", "M 0", "M 1", "M 2", "M 3", "M 4", "M 1500", "M 2995",
        "UPL #14abcd", "UPL 'f',#211\n;\"#,abc;", "UPL #3150", "UPL #15ab", "UPL #10",
        "CHAN2:X?", "CHAN:AB:Y:Z:W3:Q?", "TEMP? 'a\nb'", "X? #0zz", "BOGUS", ":X",
        "M #", "M #,1", "M # 2", "UPL #", "M #H1F",
    };
    size_t n = 0;
    unsigned count = 1u + rng() % 4u;

    for (unsigned k = 0; k < count && n + 64 < size; k++) {
        const char *p = pieces[rng() % (sizeof pieces / sizeof pieces[0])];
        size_t len = strlen(p);
        memcpy(buf + n, p, len);
        n += len;
        if (strcmp(p, "UPL #3150") == 0) {
            /* 150 bytes of block data, possibly cut short */
            size_t data = 150u - (rng() % 4u == 0 ? rng() % 150u : 0u);
            for (size_t d = 0; d < data && n < size - 8; d++) {
                buf[n++] = (char)rng();
            }
        }
        if (rng() % 16 == 0 && n > 0) {
            buf[rng() % n] = (char)rng(); /* noise */
        }
        buf[n++] = (rng() % 3 == 0) ? '\n' : ';';
    }
    return n;
}

static void fuzz_message_model(const viper_scpi_config_t *cfg, unsigned long steps)
{
    static viper_scpi_context_t ctx;
    static char pending[512];
    static size_t pending_len, pending_pos;
    uint8_t rd[128];

    if (viper_scpi_init(&ctx, cfg) != VIPER_SCPI_RES_OK) {
        s_failures++;
        return;
    }
    pending_len = pending_pos = 0;
    for (unsigned long i = 0; i < steps; i++) {
        uint32_t op = rng() % 100u;

        if (op < 50) {
            /* Feed the next random-sized piece of the current message. */
            if (pending_pos == pending_len) {
                pending_len = make_message(pending, sizeof pending);
                pending_pos = 0;
            }
            if (!viper_scpi_busy(&ctx) || rng() % 8 == 0) {
                size_t n = 1u + rng() % 40u;
                if (n > pending_len - pending_pos) {
                    n = pending_len - pending_pos;
                }
                viper_scpi_input_buffer(&ctx, pending + pending_pos, n);
                pending_pos += n;
            }
        } else if (op < 62) {
            if (pending_pos == pending_len || rng() % 4 == 0) {
                viper_scpi_end_of_message(&ctx);
                pending_pos = pending_len;
            }
        } else if (op < 82) {
            size_t max = rng() % (sizeof rd + 1);
            bool end = false;
            size_t got = viper_scpi_output_read(&ctx, rd, max, &end);
            FUZZ_ASSERT(got <= max);
            s_bytes_read += got;
            if (end && got > 0) {
                s_reply_ends++;
            }
            if (end) {
                FUZZ_ASSERT(!viper_scpi_output_pending(&ctx));
                FUZZ_ASSERT((viper_scpi_status_byte(&ctx) & VIPER_SCPI_STB_MAV) == 0);
            }
        } else if (op < 84) {
            viper_scpi_output_discard(&ctx);
            FUZZ_ASSERT(!viper_scpi_output_pending(&ctx));
        } else if (op < 85) {
            viper_scpi_device_clear(&ctx);
            FUZZ_ASSERT(!viper_scpi_output_pending(&ctx));
            FUZZ_ASSERT(!viper_scpi_busy(&ctx));
            pending_pos = pending_len;
        } else if (op < 86) {
            viper_scpi_input_discard(&ctx);
            pending_pos = pending_len;
        } else if (op < 92) {
            viper_scpi_op_end(&ctx);
            viper_scpi_poll(&ctx);
        } else if (op < 96) {
            (void)viper_scpi_take_service_request(&ctx);
            if (rng() % 2) {
                (void)viper_scpi_serial_poll(&ctx);
            } else {
                viper_scpi_rqs_delivered(&ctx);
            }
        } else {
            const char *info;
            (void)viper_scpi_error_pop_info(&ctx, &info);
            viper_scpi_status_condition(&ctx, VIPER_SCPI_STATUS_OPER, 0x10, (uint16_t)rng());
        }
        check_mav(&ctx);
        if (viper_scpi_busy(&ctx)) {
            s_busy++;
        }
    }
}

int main(void)
{
    static viper_scpi_context_t ctx;
    static const char alphabet[] =
        "#0123456789 ,;:?*\"'\nXYZWQAaCHANnelSENSEBHZKMUVSECMIN\r-+.eE"
        "IDN?*RSTCLSOPCESRSTBSYST:ERR?";
    const unsigned long iterations = 1000000ul;

    if (viper_scpi_init(&ctx, &config) != VIPER_SCPI_RES_OK) {
        return 1;
    }
    for (unsigned long i = 0; i < iterations; i++) {
        char c;

        if (rng() % 8 == 0) {
            c = (char)rng();
        } else {
            c = alphabet[rng() % (sizeof alphabet - 1)];
        }
        viper_scpi_input_char(&ctx, c);
        if (rng() % 5000 == 0) {
            viper_scpi_end_of_message(&ctx);
        }
        if (rng() % 20000 == 0) {
            (void)viper_scpi_status_byte(&ctx);
            (void)viper_scpi_error_pop(&ctx);
        }
    }
    printf("test_fuzz: %lu bytes processed (stream model)\n", iterations);

    fuzz_message_model(&msg_config, 300000ul);
    fuzz_message_model(&msg_config_eom, 300000ul);
    printf("test_fuzz: 600000 message-exchange steps, %d invariant failures\n", s_failures);
    printf("test_fuzz: coverage: %lu busy steps, %lu bytes read, %lu replies, %lu streams\n",
           s_busy, s_bytes_read, s_reply_ends, s_stream_begins);
    if (s_busy == 0 || s_bytes_read == 0 || s_reply_ends == 0 || s_stream_begins == 0) {
        printf("FAIL: fuzz coverage counter is zero\n");
        s_failures++;
    }
    return s_failures == 0 ? 0 : 1;
}
