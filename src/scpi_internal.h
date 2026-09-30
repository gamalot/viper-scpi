/* Internal helpers shared between viper-scpi translation units. */
#ifndef VIPER_SCPI_INTERNAL_H
#define VIPER_SCPI_INTERNAL_H

#include "viper_scpi/viper_scpi.h"

static inline char viper_scpi_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static inline bool viper_scpi_is_space(char c)
{
    return c == ' ' || c == '\t';
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
                                                const char *name, size_t len,
                                                bool query);

/** PARAM_NOT_ALLOWED if the command carries parameters, else OK. */
viper_scpi_result_t viper_scpi_no_params(const viper_scpi_context_t *ctx);

#endif /* VIPER_SCPI_INTERNAL_H */
