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

/** Node pattern match honouring "[optional]" and "NAME#" decorations.
 *  @p suffix receives the number or -1 if omitted; @p has_suffix tells
 *  whether the pattern takes a suffix at all. */
bool viper_scpi_node_match(const char *pattern, const char *tok, size_t len,
                           int32_t *suffix, bool *has_suffix);
bool viper_scpi_node_is_optional(const char *pattern);
bool viper_scpi_node_has_suffix(const char *pattern);

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
