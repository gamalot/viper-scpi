/* Internal helpers shared between viper-scpi translation units. */
#ifndef VIPER_SCPI_INTERNAL_H
#define VIPER_SCPI_INTERNAL_H

#include "viper_scpi/viper_scpi.h"

static inline char viper_scpi_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* IEEE 488.2 7.4.1.2 <white space>: bytes 0x00-0x09 and 0x0B-0x20 (LF is
 * not white space; it terminates or separates). */
static inline bool viper_scpi_is_space(char c)
{
    return (unsigned char)c <= 0x20u && c != '\n';
}

/** One mnemonic of a node pattern such as "TRIGger[:SEQuence]:SOURce". */
typedef struct {
    const char *name;   /**< Without brackets and without a trailing '#' */
    size_t len;
    bool optional;
    bool has_suffix;
} viper_scpi_pattern_item_t;

/** Advance @p *p to the next mnemonic. Returns 1 (item stored), 0 (end) or
 *  -1 (malformed pattern, which never matches). */
int viper_scpi_pattern_next(const char **p, viper_scpi_pattern_item_t *item);

/** True if every mnemonic of @p pattern is optional (and it is well formed). */
bool viper_scpi_pattern_all_optional(const char *pattern);

/** Match one header mnemonic against a pattern item. @p suffix receives the
 *  number, or -1 if omitted. */
bool viper_scpi_item_match(const viper_scpi_pattern_item_t *item, const char *tok,
                           size_t len, int32_t *suffix);

/**
 * If @p p (n bytes remaining, p[0] == '#') starts a complete block, returns
 * its total length including the header, else 0. An indefinite-length block
 * ("#0") extends to the end of the buffer.
 */
size_t viper_scpi_block_span(const char *p, size_t n);

/** Built-in tree searched after the user tree (SYSTem:ERRor etc.). */
extern const viper_scpi_node_t viper_scpi_builtin_root[];

/** Dispatch a common command (name without '*' and '?'). */
viper_scpi_result_t viper_scpi_ieee488_dispatch(viper_scpi_context_t *ctx,
                                                const char *name, size_t len);

/** Quoted string response made of @p a, then ";" and @p b if @p b is not
 *  NULL, with embedded quotes doubled. */
void viper_scpi_result_string_pair(viper_scpi_context_t *ctx, const char *a, const char *b);

/** MIN/MAX/DEF/UP/DOWN keyword of parameter @p idx, or VIPER_SCPI_KW_NONE. */
viper_scpi_numeric_kw_t viper_scpi_numeric_keyword(const viper_scpi_context_t *ctx, size_t idx);

/** The one parameter of a register write (*ESE, *SRE, STATus ENABle/PTR/NTR):
 *  an integer (an NRf is rounded) in 0..@p max, and no second parameter. */
viper_scpi_result_t viper_scpi_param_register(const viper_scpi_context_t *ctx, int32_t max,
                                              int32_t *out);

/** Why message execution is suspended (ctx->wait). */
enum { VIPER_SCPI_WAIT_NONE = 0, VIPER_SCPI_WAIT_WAI, VIPER_SCPI_WAIT_OPCQ, VIPER_SCPI_WAIT_DEFER };

/** Length of the leading number of @p s: [sign] digits [. digits]
 *  [E [sign] digits], 0 if there is none. An 'E' without digits is left
 *  for a unit (EX = exa). In scpi_params.c, so that parsing without units
 *  links nothing from scpi_units.c. */
size_t viper_scpi_scan_number(const char *s, size_t n);

/** Queue what a callback or stream handler returned (nothing for 0), unless
 *  it is the code viper_scpi_fail() queued during the call. Each caller sets
 *  ctx->failed to 0 before the call and ends with this. */
void viper_scpi_queue_result(viper_scpi_context_t *ctx, viper_scpi_result_t res);

/** STATus subtree, hooked into the built-in root. */
extern const viper_scpi_node_t viper_scpi_status_children[];

/** STATus:PRESet values (also the power-on state): enables 0, PTR all ones,
 *  NTR 0. Events and conditions are left alone. */
void viper_scpi_status_preset(viper_scpi_context_t *ctx);

/** The output queue holds response bytes, complete (MAV) or still being
 *  produced by the executing message. */
bool viper_scpi_output_queued(const viper_scpi_context_t *ctx);

/** Re-evaluate the service-request condition and latch a rising edge. Call
 *  after anything that can change the status byte or SRE. */
void viper_scpi_srq_update(viper_scpi_context_t *ctx);

#endif /* VIPER_SCPI_INTERNAL_H */
