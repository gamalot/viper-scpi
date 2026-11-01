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

/**
 * Device clear (IEEE 488.2 DCL/SDC, USBTMC INITIATE_CLEAR, VXI-11
 * device_clear): drop a partly received message, the compound-header path
 * and any queued response. Status registers and the error queue are left
 * alone, as 488.2 requires; MAV clears because the queue is now empty.
 */
void viper_scpi_device_clear(viper_scpi_context_t *ctx);

/**
 * Drop a partly received program message and nothing else: the queued
 * response, the status registers and the error queue are kept. For a
 * transport discarding input on its own (USBTMC INITIATE_ABORT_BULK_OUT or
 * CLEAR_FEATURE on bulk OUT, a VXI-11 link abort) while the controller may
 * still read a reply. viper_scpi_device_clear() includes it.
 */
void viper_scpi_input_discard(viper_scpi_context_t *ctx);

/** User pointer from the configuration. */
void *viper_scpi_user_data(const viper_scpi_context_t *ctx);

/** True if the command being executed was a query (header ended in '?'). */
bool viper_scpi_is_query(const viper_scpi_context_t *ctx);

/**
 * Numeric suffix of the @p index-th suffix-capable ("NAME#") node of the
 * matched header, counted from the left. Returns @p default_value when the
 * suffix was omitted (e.g. "CHAN:VOLT?" for "CHANnel#") or @p index is out
 * of range.
 */
int32_t viper_scpi_suffix(const viper_scpi_context_t *ctx, size_t index,
                          int32_t default_value);

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

/** Floating point with optional exponent, e.g. -1.23E-3, or INFinity,
 *  NINFinity (negative infinity) or NAN. */
viper_scpi_result_t viper_scpi_param_double(const viper_scpi_context_t *ctx, size_t idx,
                                            double *out);

/**
 * Number or MINimum / MAXimum / DEFault keyword. A keyword resolves to
 * @p min, @p max or @p def; a number must lie within [min, max]
 * (OUT_OF_RANGE otherwise). PARAM_INVALID if @p min > @p max.
 */
viper_scpi_result_t viper_scpi_param_double_kw(const viper_scpi_context_t *ctx, size_t idx,
                                               double min, double max, double def,
                                               double *out);
/** Integer flavour of viper_scpi_param_double_kw(). */
viper_scpi_result_t viper_scpi_param_int32_kw(const viper_scpi_context_t *ctx, size_t idx,
                                              int32_t min, int32_t max, int32_t def,
                                              int32_t *out);

/**
 * viper_scpi_param_double_kw() that also accepts UP and DOWN and reports in
 * @p kw which keyword was given (VIPER_SCPI_KW_NONE for a number). For UP and
 * DOWN @p out is left unchanged: the instrument steps its own value. In all
 * the range-checked parsers INF/NINF must lie within [min, max] and NAN is
 * ILLEGAL_VALUE (-224).
 */
viper_scpi_result_t viper_scpi_param_double_kw_ex(const viper_scpi_context_t *ctx, size_t idx,
                                                  double min, double max, double def,
                                                  double *out, viper_scpi_numeric_kw_t *kw);
/** Integer flavour of viper_scpi_param_double_kw_ex(). */
viper_scpi_result_t viper_scpi_param_int32_kw_ex(const viper_scpi_context_t *ctx, size_t idx,
                                                 int32_t min, int32_t max, int32_t def,
                                                 int32_t *out, viper_scpi_numeric_kw_t *kw);

/**
 * Number with an optional SCPI unit suffix, converted to the base unit.
 * Accepts "1.5KHZ", "100 mV", "2.5E3HZ", "5MA" and a bare number (no unit).
 * Multipliers: EX PE T G MA K M U N P F A (MA = mega, M = milli, except where
 * a unit sets VIPER_SCPI_UNIT_M_IS_MEGA, e.g. MHZ). @p units lists the unit
 * names accepted. An unknown or disallowed suffix gives INVALID_SUFFIX (-131).
 */
viper_scpi_result_t viper_scpi_param_double_unit(const viper_scpi_context_t *ctx, size_t idx,
                                                 const viper_scpi_unit_t *units, double *out);

/** viper_scpi_param_double_unit() combined with MIN/MAX/DEF and a range check
 *  (in base units), like viper_scpi_param_double_kw(). */
viper_scpi_result_t viper_scpi_param_double_unit_kw(const viper_scpi_context_t *ctx, size_t idx,
                                                    const viper_scpi_unit_t *units,
                                                    double min, double max, double def,
                                                    double *out);
/** viper_scpi_param_double_unit_kw() with UP/DOWN, like
 *  viper_scpi_param_double_kw_ex(). */
viper_scpi_result_t viper_scpi_param_double_unit_kw_ex(const viper_scpi_context_t *ctx,
                                                       size_t idx, const viper_scpi_unit_t *units,
                                                       double min, double max, double def,
                                                       double *out, viper_scpi_numeric_kw_t *kw);

/** Ready-made unit tables. */
extern const viper_scpi_unit_t viper_scpi_units_volt[];    /* V */
extern const viper_scpi_unit_t viper_scpi_units_amp[];     /* A */
extern const viper_scpi_unit_t viper_scpi_units_hertz[];   /* HZ, MHZ = mega */
extern const viper_scpi_unit_t viper_scpi_units_ohm[];     /* OHM, MOHM = mega */
extern const viper_scpi_unit_t viper_scpi_units_watt[];    /* W */
extern const viper_scpi_unit_t viper_scpi_units_farad[];   /* F */
extern const viper_scpi_unit_t viper_scpi_units_henry[];   /* H */
extern const viper_scpi_unit_t viper_scpi_units_decibel[]; /* DB */
extern const viper_scpi_unit_t viper_scpi_units_second[];  /* S, SEC, MIN, HOUR */
extern const viper_scpi_unit_t viper_scpi_units_celsius[]; /* CEL */

/**
 * IEEE 488.2 arbitrary block data: definite length (#<n><length><data>) or
 * indefinite length (#0<data>, which must be the last item of the line).
 * @p data points into the line buffer and is valid only during the callback.
 */
viper_scpi_result_t viper_scpi_param_block(const viper_scpi_context_t *ctx, size_t idx,
                                           const uint8_t **data, size_t *len);

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
/** NR3/NR2 response with VIPER_SCPI_DOUBLE_DIGITS significant digits and an
 *  upper-case exponent (1E+10). +-INF give +-9.9E+37, NaN 9.91E+37. */
void viper_scpi_result_double(viper_scpi_context_t *ctx, double value);
/** viper_scpi_result_double() with @p digits significant digits (clamped to
 *  1..17), e.g. 7 for a 6.5-digit meter. */
void viper_scpi_result_double_digits(viper_scpi_context_t *ctx, double value, int digits);
void viper_scpi_result_bool(viper_scpi_context_t *ctx, bool value);
/** Definite-length block: "#<n><length>" header followed by raw bytes. */
void viper_scpi_result_block(viper_scpi_context_t *ctx, const void *data, size_t len);
/**
 * Definite-length block whose bytes are produced on demand by @p fill, for
 * responses too large to hold in RAM. With an output buffer the bytes are
 * generated while the transport reads them (one such block per response;
 * a second gives -400). Without one they are generated in small chunks and
 * passed to @c write immediately.
 */
void viper_scpi_result_block_fill(viper_scpi_context_t *ctx, uint32_t len,
                                  viper_scpi_block_fill_t fill, void *fill_user);
/** Quoted string with embedded quotes doubled. */
void viper_scpi_result_string(viper_scpi_context_t *ctx, const char *str);
/** Unquoted text (e.g. character data or *IDN? fields). */
void viper_scpi_result_text(viper_scpi_context_t *ctx, const char *str);

/* ---- Error queue ---------------------------------------------------- */

/** Append an error (also sets the matching ESR bit). Overflow -> -350. */
void viper_scpi_error_push(viper_scpi_context_t *ctx, int code);
/**
 * viper_scpi_error_push() with detail that SYSTem:ERRor? appends to the
 * message: -222,"Data out of range;VOLT 31 exceeds 30". @p info is not copied
 * and must stay valid until the error is read (static storage, a string
 * literal). If the queue is full the entry becomes -350 without detail.
 */
void viper_scpi_error_push_info(viper_scpi_context_t *ctx, int code, const char *info);
/** Remove the oldest error; returns 0 if the queue is empty. */
int viper_scpi_error_pop(viper_scpi_context_t *ctx);
/** viper_scpi_error_pop() that also returns the entry's detail (or NULL). */
int viper_scpi_error_pop_info(viper_scpi_context_t *ctx, const char **info);
size_t viper_scpi_error_count(const viper_scpi_context_t *ctx);
void viper_scpi_error_clear(viper_scpi_context_t *ctx);
/** Built-in message text for @p code ("Device-specific error" if unknown). */
const char *viper_scpi_error_message(int code);
/** Message text for @p code: the configuration's error_texts first, then the
 *  built-in texts. */
const char *viper_scpi_error_text(const viper_scpi_context_t *ctx, int code);

/* ---- Output queue (message-exchange model, cfg->output_buffer) ------ */

/** MAV: a response is waiting to be read. Always false in the stream model. */
bool viper_scpi_output_pending(const viper_scpi_context_t *ctx);

/**
 * Copy up to @p max bytes of the queued response to @p dst. @p *end is set
 * when this read reaches the end of the response message (the queue is then
 * empty); a transport marks its last chunk with it (USBTMC EOM, VXI-11 END).
 */
size_t viper_scpi_output_read(viper_scpi_context_t *ctx, uint8_t *dst, size_t max,
                              bool *end);

/** Drop what is left of the queued response (e.g. USBTMC ABORT_BULK_IN). */
void viper_scpi_output_discard(viper_scpi_context_t *ctx);

/* ---- Overlapped commands --------------------------------------------- *
 * A callback that starts a slow operation calls viper_scpi_op_begin() and
 * returns; the application calls viper_scpi_op_end() when it finishes. While
 * any operation is pending, *OPC sets OPC only on completion, *OPC? answers
 * only on completion, and *WAI suspends the rest of the message. Suspended
 * execution resumes from viper_scpi_poll(). Call all of these from the same
 * context as the input functions (not from an interrupt). */

/** One more operation is pending. */
void viper_scpi_op_begin(viper_scpi_context_t *ctx);
/** One pending operation finished. When none remain, an armed *OPC sets OPC
 *  in the ESR (which can raise a service request). Ignored if none pending. */
void viper_scpi_op_end(viper_scpi_context_t *ctx);
/** True while at least one operation is pending. */
bool viper_scpi_op_pending(const viper_scpi_context_t *ctx);
/**
 * True while a message is suspended by *WAI or by *OPC? waiting for pending
 * operations. Input is refused meanwhile: a transport holds off the
 * controller (USBTMC NAKs bulk-OUT, a UART deasserts RTS). Bytes fed anyway
 * are dropped and reported as -363 at their message end.
 */
bool viper_scpi_busy(const viper_scpi_context_t *ctx);
/** Resume a suspended message if no operation is pending; call it from the
 *  main loop. Does nothing otherwise. */
void viper_scpi_poll(viper_scpi_context_t *ctx);

/* ---- Status registers ------------------------------------------------ */

/** Set Event Status Register bits (e.g. VIPER_SCPI_ESR_URQ). */
void viper_scpi_event_set(viper_scpi_context_t *ctx, uint8_t mask);
/** Current status byte, including QUES (bit 3), EAV, MAV (message-exchange
 *  model), ESB, MSS and OPER (bit 7). PON is set in the ESR by
 *  viper_scpi_init(). */
uint8_t viper_scpi_status_byte(const viper_scpi_context_t *ctx);

/**
 * True once per new service request: a false-to-true transition of
 * (status byte & SRE). Transitions are tracked as they happen -- after every
 * command, error and output-queue change -- so "*CLS;*SRE 32;*OPC" on one line
 * still yields one, although the condition is true before and after the line.
 * The transport answers it with its SRQ mechanism (USB488 interrupt-IN
 * notification, VXI-11 device_intr_srq, asserting SRQ on GPIB).
 */
bool viper_scpi_take_service_request(viper_scpi_context_t *ctx);

/**
 * Update the condition register of a SCPI status set: the bits in @p mask take
 * their values from @p value (bit 15 is ignored). Changes pass the transition
 * filters (PTRansition for 0 -> 1, NTRansition for 1 -> 0) into the event
 * register, whose enabled bits make up the summary in the status byte (OPER =
 * bit 7, QUES = bit 3) and can raise a service request.
 * Example: viper_scpi_status_condition(ctx, VIPER_SCPI_STATUS_OPER, 1u << 4,
 * measuring ? 1u << 4 : 0) for the standard MEASuring bit.
 */
void viper_scpi_status_condition(viper_scpi_context_t *ctx, viper_scpi_status_reg_t reg,
                                 uint16_t mask, uint16_t value);

/** Read-only view of a status register set, for diagnostics. */
const viper_scpi_status_set_t *viper_scpi_status_get(const viper_scpi_context_t *ctx,
                                                     viper_scpi_status_reg_t reg);

/**
 * Serial poll (GPIB serial poll, USB488 READ_STATUS_BYTE, VXI-11
 * device_readstb): the status byte with RQS in bit 6 instead of MSS, then
 * RQS is cleared. RQS is set when viper_scpi_take_service_request() reports
 * a request, and also clears if the request condition goes away before the
 * poll (IEEE 488.1: the device stops requesting service). *STB? keeps
 * reporting MSS and never clears anything.
 */
uint8_t viper_scpi_serial_poll(viper_scpi_context_t *ctx);

/**
 * Clear RQS without a poll, for transports whose SRQ message itself carries
 * the status byte (USB488 interrupt-IN notification): the host's VISA hands
 * that byte to the next viReadSTB() without asking the device, so delivering
 * the notification is the serial poll.
 */
void viper_scpi_rqs_delivered(viper_scpi_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* VIPER_SCPI_H */
