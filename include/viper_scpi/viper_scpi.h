/**
 * @file viper_scpi.h
 * @brief Public API of viper-scpi, a zero-allocation SCPI parser for C99.
 */
#ifndef VIPER_SCPI_H
#define VIPER_SCPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "viper_scpi/scpi_config.h"
#include "viper_scpi/scpi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Core ---------------------------------------------------------- */

/** Initialise @p ctx with @p cfg. Returns PARAM_INVALID on NULL arguments. */
viper_scpi_result_t viper_scpi_init(viper_scpi_context_t *ctx,
                                    const viper_scpi_config_t *cfg);

/** Feed one byte. A '\n' executes the buffered line; '\r' is ignored. */
void viper_scpi_input_char(viper_scpi_context_t *ctx, char c);

/** Feed @p len bytes. */
void viper_scpi_input_buffer(viper_scpi_context_t *ctx, const char *data, size_t len);

/** Execute the buffered line without a '\n' (e.g. USBTMC EOM). */
void viper_scpi_end_of_message(viper_scpi_context_t *ctx);

/** User pointer from the configuration. */
void *viper_scpi_user_data(const viper_scpi_context_t *ctx);

/** True if the command being executed was a query (header ended in '?'). */
bool viper_scpi_is_query(const viper_scpi_context_t *ctx);

/** Case-insensitive long/short form match of @p tok against @p pattern. */
bool viper_scpi_pattern_match(const char *pattern, const char *tok, size_t len);

/* ---- Parameters (valid inside callbacks; index is zero based) ------- */

size_t viper_scpi_param_count(const viper_scpi_context_t *ctx);
bool viper_scpi_param_present(const viper_scpi_context_t *ctx, size_t idx);

/** Raw token. MISSING_PARAM if @p idx is out of range. */
viper_scpi_result_t viper_scpi_param_raw(const viper_scpi_context_t *ctx, size_t idx,
                                         const char **str, size_t *len);

/** Decimal, 0x hex, #H hex, #B binary, #Q octal. */
viper_scpi_result_t viper_scpi_param_int32(const viper_scpi_context_t *ctx, size_t idx,
                                           int32_t *out);
viper_scpi_result_t viper_scpi_param_uint32(const viper_scpi_context_t *ctx, size_t idx,
                                            uint32_t *out);

/** Floating point with optional exponent, e.g. -1.23E-3. */
viper_scpi_result_t viper_scpi_param_double(const viper_scpi_context_t *ctx, size_t idx,
                                            double *out);

/** ON / OFF / 1 / 0. */
viper_scpi_result_t viper_scpi_param_bool(const viper_scpi_context_t *ctx, size_t idx,
                                          bool *out);

/** Keyword from @p choices (long or short form). ILLEGAL_VALUE if unknown. */
viper_scpi_result_t viper_scpi_param_choice(const viper_scpi_context_t *ctx, size_t idx,
                                            const viper_scpi_choice_t *choices, int *out);

/** Quoted string ("..." or '...'), unescaped and NUL terminated into @p out. */
viper_scpi_result_t viper_scpi_param_string(const viper_scpi_context_t *ctx, size_t idx,
                                            char *out, size_t out_size, size_t *out_len);

/* ---- Responses (call from query callbacks) --------------------------
 * Multiple values in one query are separated by ',', responses of
 * different commands on one line by ';', and the line ends with '\n'. */

void viper_scpi_result_int(viper_scpi_context_t *ctx, int32_t value);
void viper_scpi_result_uint(viper_scpi_context_t *ctx, uint32_t value);
void viper_scpi_result_double(viper_scpi_context_t *ctx, double value);
void viper_scpi_result_bool(viper_scpi_context_t *ctx, bool value);
/** Quoted string with embedded quotes doubled. */
void viper_scpi_result_string(viper_scpi_context_t *ctx, const char *str);
/** Unquoted text (e.g. character data or *IDN? fields). */
void viper_scpi_result_text(viper_scpi_context_t *ctx, const char *str);

/* ---- Error queue ---------------------------------------------------- */

/** Append an error (also sets the matching ESR bit). Overflow -> -350. */
void viper_scpi_error_push(viper_scpi_context_t *ctx, int code);
/** Remove the oldest error; returns 0 if the queue is empty. */
int viper_scpi_error_pop(viper_scpi_context_t *ctx);
size_t viper_scpi_error_count(const viper_scpi_context_t *ctx);
void viper_scpi_error_clear(viper_scpi_context_t *ctx);
/** Standard message text for @p code. */
const char *viper_scpi_error_message(int code);

/* ---- Status registers ------------------------------------------------ */

/** Set Event Status Register bits (e.g. VIPER_SCPI_ESR_URQ). */
void viper_scpi_event_set(viper_scpi_context_t *ctx, uint8_t mask);
/** Current status byte, including EAV, ESB and MSS. */
uint8_t viper_scpi_status_byte(const viper_scpi_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* VIPER_SCPI_H */
