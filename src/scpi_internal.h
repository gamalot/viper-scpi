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

/** Built-in tree searched after the user tree (SYSTem:ERRor etc.). */
extern const viper_scpi_node_t viper_scpi_builtin_root[];

/** Dispatch a common command (name without '*' and '?'). */
viper_scpi_result_t viper_scpi_ieee488_dispatch(viper_scpi_context_t *ctx,
                                                const char *name, size_t len,
                                                bool query);

/** PARAM_NOT_ALLOWED if the command carries parameters, else OK. */
viper_scpi_result_t viper_scpi_no_params(const viper_scpi_context_t *ctx);

#endif /* VIPER_SCPI_INTERNAL_H */
