/**
 * @file scpi_types.h
 * @brief Public data structures of viper-scpi.
 */
#ifndef VIPER_SCPI_TYPES_H
#define VIPER_SCPI_TYPES_H

#include <stddef.h>
#include <stdint.h>

#include "viper_scpi/scpi_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * An error number, and the result of callbacks and parameter functions:
 * 0 for success, a negative IEEE 488.2 / SCPI error number, or a positive
 * device-defined one (SCPI-99 21.8.2). The one type of every error code in
 * the API, from a callback's return value to the error queue, sized to the
 * range of SCPI error numbers (-32768..32767).
 */
typedef int16_t viper_scpi_result_t;

/** Error numbers the library uses, for viper_scpi_result_t. */
enum {
    VIPER_SCPI_RES_OK = 0,
    VIPER_SCPI_RES_ERR_UNKNOWN_CMD = -100,      /**< Command error */
    VIPER_SCPI_RES_ERR_SYNTAX = -102,           /**< Syntax error */
    VIPER_SCPI_RES_ERR_DATA_TYPE = -104,        /**< Data type error */
    VIPER_SCPI_RES_ERR_PARAM_NOT_ALLOWED = -108,/**< Parameter not allowed */
    VIPER_SCPI_RES_ERR_MISSING_PARAM = -109,    /**< Missing parameter */
    VIPER_SCPI_RES_ERR_MNEMONIC_TOO_LONG = -112,/**< Program mnemonic too long */
    VIPER_SCPI_RES_ERR_UNDEFINED_HEADER = -113, /**< Undefined header */
    VIPER_SCPI_RES_ERR_SUFFIX_RANGE = -114,     /**< Header suffix out of range */
    VIPER_SCPI_RES_ERR_INVALID_SUFFIX = -131,   /**< Invalid suffix (unit) */
    VIPER_SCPI_RES_ERR_BLOCK_DATA = -160,       /**< Block data error */
    VIPER_SCPI_RES_ERR_INVALID_BLOCK = -161,    /**< Invalid block data (e.g. cut short) */
    VIPER_SCPI_RES_ERR_EXECUTION = -200,        /**< Execution error */
    VIPER_SCPI_RES_ERR_PARAM_INVALID = -220,    /**< Parameter error */
    VIPER_SCPI_RES_ERR_OUT_OF_RANGE = -222,     /**< Data out of range */
    VIPER_SCPI_RES_ERR_ILLEGAL_VALUE = -224,    /**< Illegal parameter value */
    VIPER_SCPI_RES_ERR_OUT_OF_MEMORY = -321,    /**< Out of memory (output queue full) */
    VIPER_SCPI_RES_ERR_QUEUE_OVERFLOW = -350,   /**< Queue overflow */
    VIPER_SCPI_RES_ERR_INPUT_OVERRUN = -363,    /**< Input buffer overrun */
    VIPER_SCPI_RES_ERR_QUERY = -400,            /**< Query error */
    VIPER_SCPI_RES_ERR_QUERY_INTERRUPTED = -410,/**< New message over an unread reply */
    VIPER_SCPI_RES_ERR_QUERY_UNTERMINATED = -420,/**< Read requested, nothing to send */
    VIPER_SCPI_RES_ERR_QUERY_INDEFINITE = -440   /**< Query after an indefinite
                                                      response (*IDN?) */
};

/** Event Status Register bits (IEEE 488.2). */
enum {
    VIPER_SCPI_ESR_OPC = 0x01, /**< Operation complete */
    VIPER_SCPI_ESR_RQC = 0x02, /**< Request control */
    VIPER_SCPI_ESR_QYE = 0x04, /**< Query error */
    VIPER_SCPI_ESR_DDE = 0x08, /**< Device dependent error */
    VIPER_SCPI_ESR_EXE = 0x10, /**< Execution error */
    VIPER_SCPI_ESR_CME = 0x20, /**< Command error */
    VIPER_SCPI_ESR_URQ = 0x40, /**< User request */
    VIPER_SCPI_ESR_PON = 0x80  /**< Power on */
};

/** Status Byte bits (IEEE 488.2). */
enum {
    VIPER_SCPI_STB_EAV = 0x04, /**< Error queue not empty */
    VIPER_SCPI_STB_QUES = 0x08,/**< QUEStionable summary (SCPI) */
    VIPER_SCPI_STB_MAV = 0x10, /**< Message available */
    VIPER_SCPI_STB_ESB = 0x20, /**< Event status summary */
    VIPER_SCPI_STB_MSS = 0x40, /**< Master summary status (*STB?) */
    VIPER_SCPI_STB_RQS = 0x40, /**< Request service (serial poll), same bit */
    VIPER_SCPI_STB_OPER = 0x80 /**< OPERation summary (SCPI) */
};

typedef struct viper_scpi_context_s viper_scpi_context_t;

/** Command callback. A non-zero return value is pushed to the error queue,
 *  unless the callback already queued it with viper_scpi_fail(). */
typedef viper_scpi_result_t (*viper_scpi_cmd_callback_t)(viper_scpi_context_t *ctx);

/**
 * Command tree node (intended to live in const/Flash memory).
 *
 * The pattern uses the SCPI long/short form convention: the leading run of
 * upper-case letters is the short form, the whole string is the long form
 * ("VOLTage" matches VOLT and VOLTAGE, case-insensitively).
 *
 * A pattern is a path of one or more mnemonics separated by ':' and may use
 * two decorations:
 *  - "[:LEVel]" or "[LEVel]"  optional mnemonic that may be omitted from the
 *               header. "VOLTage[:LEVel]" matches VOLT and VOLT:LEV.
 *  - "CHANnel#" numeric suffix: CHAN, CHAN2 and CHANNEL12 all match; the
 *               number is read with viper_scpi_suffix().
 * They combine, e.g. "TRIGger[:SEQuence]:SOURce" or "[CHANnel#]". Optional
 * brackets do not nest. A node whose mnemonics are all optional may be left
 * out of the header entirely. Sibling arrays are terminated by
 * VIPER_SCPI_NODE_END.
 */
typedef struct viper_scpi_node_s {
    const char *pattern;                     /**< e.g. "VOLTage"; NULL ends a list */
    viper_scpi_cmd_callback_t callback;      /**< NULL for intermediate nodes */
    const struct viper_scpi_node_s *children;/**< NULL for leaf nodes */
} viper_scpi_node_t;

/** List terminator for arrays of viper_scpi_node_t. */
#define VIPER_SCPI_NODE_END { NULL, NULL, NULL }

/** Unit flags for viper_scpi_unit_t. */
enum {
    VIPER_SCPI_UNIT_NO_PREFIX = 1, /**< No multiplier prefix (MIN, HOUR, CEL) */
    VIPER_SCPI_UNIT_M_IS_MEGA = 2  /**< "M" means mega, not milli (MHZ, MOHM) */
};

/** One unit of a unit list; lists end with a NULL name. */
typedef struct {
    const char *name;  /**< Case-insensitive, e.g. "HZ" */
    double scale;      /**< Factor to the base unit (1.0 unless e.g. MIN = 60) */
    uint8_t flags;     /**< VIPER_SCPI_UNIT_* */
} viper_scpi_unit_t;

/**
 * The units a number spec accepts: a unit list and the parser for it.
 * Define one with VIPER_SCPI_UNIT_TABLE(); the parser is reached only through
 * tables, so firmware that uses none does not link it.
 */
typedef struct {
    /** Splits a number from its unit; VIPER_SCPI_UNIT_TABLE() sets it. */
    viper_scpi_result_t (*split)(const viper_scpi_unit_t *list, const char *s, size_t n,
                                 size_t *num, const viper_scpi_unit_t **unit, int *exp3);
    const viper_scpi_unit_t *list; /**< Ends with a NULL name */
} viper_scpi_unit_table_t;

/** Numeric keyword reported by viper_scpi_param_number() / _integer(). */
typedef enum {
    VIPER_SCPI_KW_NONE = 0, /**< A number was given */
    VIPER_SCPI_KW_MIN,      /**< MINimum */
    VIPER_SCPI_KW_MAX,      /**< MAXimum */
    VIPER_SCPI_KW_DEF,      /**< DEFault */
    VIPER_SCPI_KW_UP,       /**< UP: step the current value up */
    VIPER_SCPI_KW_DOWN      /**< DOWN: step the current value down */
} viper_scpi_numeric_kw_t;

/** Flags of viper_scpi_number_t and viper_scpi_integer_t. */
enum {
    VIPER_SCPI_NUM_UPDOWN = 1 /**< Accept UP and DOWN (reported in @c kw) */
};

/**
 * A range-checked double parameter, for viper_scpi_param_number(). Keep it
 * const (in Flash) next to the callback; one spec serves the command and the
 * query ("VOLT? MAX"). Write it with designated initialisers:
 *
 *     static const viper_scpi_number_t volt = {
 *         .min = 0.0, .max = 30.0, .def = 5.0, .units = &viper_scpi_units_volt };
 *
 * Both spec types start min, max, def, units, flags.
 */
typedef struct {
    double min;                            /**< MINimum, and the lower limit */
    double max;                            /**< MAXimum, and the upper limit */
    double def;                            /**< DEFault */
    const viper_scpi_unit_table_t *units;  /**< Accepted units, NULL for a bare
                                                number; limits are in the base unit */
    uint8_t flags;                         /**< VIPER_SCPI_NUM_* */
} viper_scpi_number_t;

/**
 * A range-checked integer parameter in a scaled unit, for
 * viper_scpi_param_integer(): the value is a count of 10^exp10 base units.
 * With exp10 = -3 and the volt table, "1.5", "1500 MV" and "1.5E3MV" all
 * give 1500 (mV). Parsed without floating point.
 *
 *     static const viper_scpi_integer_t mvolt = {
 *         .min = 0, .max = 30000, .def = 5000, .units = &viper_scpi_units_volt,
 *         .exp10 = -3 };
 */
typedef struct {
    int32_t min;                           /**< MINimum, and the lower limit */
    int32_t max;                           /**< MAXimum, and the upper limit */
    int32_t def;                           /**< DEFault */
    const viper_scpi_unit_table_t *units;  /**< Accepted units, NULL for a bare
                                                number. Units with a scale other
                                                than 1 (MIN, HOUR) are refused. */
    uint8_t flags;                         /**< VIPER_SCPI_NUM_* */
    int16_t exp10;                         /**< Unit of the result: 10^exp10 base
                                                units (0 for plain counts) */
} viper_scpi_integer_t;

/** Keyword/value pair for viper_scpi_param_choice(); lists end with NULL name. */
typedef struct {
    const char *name;
    int value;
} viper_scpi_choice_t;

/** Output function (stream model): send @p len bytes to the transport and
 *  return how many it took. A short count ends the response message there:
 *  the rest of it is not written and -321 is queued, so a host times out
 *  instead of reading a truncated value as valid. Block, or buffer, rather
 *  than return less. */
typedef size_t (*viper_scpi_write_t)(void *user_data, const char *data, size_t len);

/** Optional *RST / *TRG / *CLS hook. */
typedef void (*viper_scpi_reset_t)(viper_scpi_context_t *ctx);
typedef void (*viper_scpi_trigger_t)(viper_scpi_context_t *ctx);

/** Optional *TST? hook: run the self-test and return its result, 0 for
 *  passed (IEEE 488.2 10.38). */
typedef int32_t (*viper_scpi_self_test_t)(viper_scpi_context_t *ctx);

/**
 * Producer for viper_scpi_result_block_fill(): write @p count bytes of the
 * block, starting @p offset bytes into it, to @p dst.
 */
typedef void (*viper_scpi_block_fill_t)(void *fill_user, uint8_t *dst,
                                        uint32_t offset, size_t count);

/** Events delivered to a streaming block handler. */
typedef enum {
    VIPER_SCPI_STREAM_BEGIN, /**< Block starts; len = declared length. The
                                  parameters before the block are readable. */
    VIPER_SCPI_STREAM_DATA,  /**< len bytes of the block at data */
    VIPER_SCPI_STREAM_END,   /**< Block and command complete; results allowed */
    VIPER_SCPI_STREAM_ABORT  /**< Cut short (EOM, device clear, input discard,
                                  or a parameter after the block) */
} viper_scpi_stream_event_t;

/**
 * Receives a definite-length block in chunks, for blocks larger than the line
 * buffer (MMEM:DATA, TRAC:DATA). A non-zero return from BEGIN, DATA or END is
 * pushed to the error queue (unless viper_scpi_fail() queued it); after one
 * from BEGIN or DATA the rest of the block is skipped and the handler is not
 * called again for it. ABORT's return value is ignored. A handler may call
 * viper_scpi_device_clear(): it gets no ABORT for that, and the rest of the
 * block and of the message is dropped.
 */
typedef viper_scpi_result_t (*viper_scpi_block_stream_t)(viper_scpi_context_t *ctx,
                                                         viper_scpi_stream_event_t event,
                                                         const uint8_t *data, size_t len);

/** Opt-in of a tree node to streaming block input; lists end with a NULL node. */
typedef struct {
    const viper_scpi_node_t *node;       /**< Address of the node in the tree */
    viper_scpi_block_stream_t handler;
} viper_scpi_stream_node_t;

/** Leniency options for viper_scpi_config_t.options (all off by default;
 *  the default is the standard behaviour). */
enum {
    /** A relative header that does not resolve against the current path is
     *  retried from the root: SOUR:VOLT 1;SOUR:CURR 2 then works, although
     *  SCPI-99 6.2.4 reads the second as SOUR:SOUR:CURR (-113). */
    VIPER_SCPI_OPT_RELATIVE_FALLBACK = 1u << 0,
    /** Message-exchange model only: LF separates commands like ';' (and
     *  resets the path), and only EOM/END ends the program message, so
     *  "*IDN?<LF>*OPC?<LF>" in one transport message is one message with
     *  the responses joined by ';' instead of -410 for the first. */
    VIPER_SCPI_OPT_EOM_MESSAGES = 1u << 1
};

/** Instrument error text; tables end with a NULL text. */
typedef struct {
    viper_scpi_result_t code; /**< e.g. 100..32767 for device-defined errors */
    const char *text;
} viper_scpi_error_text_t;

/**
 * Static configuration; must outlive the context. All fields optional
 * except that, without an output buffer, a NULL @c write discards responses.
 *
 * Two output models:
 *  - Stream (UART, raw TCP): leave @c output_buffer NULL. Responses go to
 *    @c write as they are produced.
 *  - Message exchange (USBTMC, VXI-11, HiSLIP, GPIB device): set
 *    @c output_buffer. Responses are queued and the transport pulls them with
 *    viper_scpi_output_read() when the controller asks. The status byte then
 *    carries MAV, and a new program message arriving over an unread reply
 *    discards it with -410 (IEEE 488.2 6.3.2.3). @c write is not used.
 */
typedef struct {
    const viper_scpi_node_t *root; /**< User command tree (may be NULL) */
    viper_scpi_write_t write;      /**< Response output (stream model) */
    void *user_data;               /**< Passed to write and available to callbacks */
    viper_scpi_reset_t reset;      /**< Called by *RST */
    const char *idn_manufacturer;  /**< *IDN? field 1 */
    const char *idn_model;         /**< *IDN? field 2 */
    const char *idn_serial;        /**< *IDN? field 3 */
    const char *idn_firmware;      /**< *IDN? field 4 */
    char *output_buffer;           /**< Response queue (message-exchange model) */
    size_t output_size;            /**< Its size; one response message must fit,
                                        except data from result_block_fill() */
    viper_scpi_trigger_t trigger;  /**< *TRG; without it *TRG is -113 */
    const viper_scpi_stream_node_t *stream_nodes; /**< Streaming block input */
    const viper_scpi_error_text_t *error_texts;   /**< Searched before the
                                                       built-in texts */
    uint32_t options;              /**< VIPER_SCPI_OPT_* leniencies */
    /** The instrument's own common commands (*SAV, *RCL, *OPT?, *CAL?,
     *  ...): nodes whose patterns are the names without '*', e.g. "SAV".
     *  Searched before the built-in ones, so an entry here replaces one. */
    const viper_scpi_node_t *common;
    viper_scpi_reset_t clear;      /**< Called by *CLS after the library's
                                        status data is cleared (clear your
                                        own event registers here) */
    viper_scpi_self_test_t self_test; /**< *TST?; without it *TST? answers 0 */
} viper_scpi_config_t;

/** SCPI status register sets, for viper_scpi_status_condition(). */
typedef enum {
    VIPER_SCPI_STATUS_OPER = 0, /**< STATus:OPERation (STB bit 7) */
    VIPER_SCPI_STATUS_QUES = 1  /**< STATus:QUEStionable (STB bit 3) */
} viper_scpi_status_reg_t;

/** One SCPI status register set (16 bits each; bit 15 is always 0). */
typedef struct {
    uint16_t condition;
    uint16_t event;
    uint16_t enable;
    uint16_t ptr;       /**< Positive (0 -> 1) transition filter */
    uint16_t ntr;       /**< Negative (1 -> 0) transition filter */
} viper_scpi_status_set_t;

/** A token of the line buffer: offset and length (not NUL terminated). */
typedef struct {
    uint16_t off;
    uint16_t len;
} viper_scpi_token_t;

/** Entries of the relative-path base: the path minus its last mnemonic. */
#define VIPER_SCPI_PREFIX_SIZE \
    ((VIPER_SCPI_MAX_RECURSION_DEPTH) - 1)

/**
 * Parser context. Allocate it statically; treat all fields as private and
 * use the accessor functions in viper_scpi.h. The fields used for every
 * input byte come first, where Thumb-1 loads reach them directly.
 */
struct viper_scpi_context_s {
    const viper_scpi_config_t *cfg;
    uint32_t block_remaining;
    uint16_t length;
    uint16_t unit_start;   /**< Buffer offset where the current unit starts */
    uint16_t hash_pos;     /**< Buffer offset of the '#' of the current block */
    uint16_t exec_pos;     /**< Resume point of a suspended message; while
                                streaming, the end of the streamed header */
    uint16_t fail_from;    /**< Error count + 1 when viper_scpi_fail() first
                                queued during the running callback, else 0:
                                the codes queued since are not queued again */
    uint16_t op_count;     /**< Pending operations (op_begin - op_end) */
    uint8_t wait;          /**< WAIT_* : why execution is suspended */
    uint8_t drop;          /**< Why the message is dropped (DROP_*), or 0 */
    uint8_t skip_unit;     /**< Rest of the unit is lexed but not kept */
    uint8_t block_state;
    char quote;
    char last;
    uint8_t block_digits;
    uint8_t msg_started;   /**< A message is executing (or streaming, or
                                suspended); its response is not complete */
    uint8_t running;       /**< RUN_*: units, or a stream handler, are executing */
    uint8_t resumed;       /**< The callback is re-run after a defer */
    uint8_t is_query;
    uint8_t param_count;
    uint8_t suffix_count;
    uint8_t prefix_count;
    uint8_t line_has_output;
    uint8_t indefinite;    /**< An indefinite response (*IDN?) ended the reply:
                                a later query of the message is -440 */
    uint8_t unit_has_output;
    uint8_t err_head;
    uint8_t err_count;
    uint8_t esr;
    uint8_t ese;
    uint8_t sre;
    uint8_t srq_prev;      /**< Service-request summary at the last check */
    uint8_t srq_new;       /**< Rising edge not yet taken */
    uint8_t rqs;           /**< Service requested and not yet polled */
    uint8_t opc_armed;     /**< *OPC given while operations were pending */
    uint8_t out_block_active;
    uint8_t out_overflow;
    char buffer[VIPER_SCPI_BUFFER_SIZE];
    viper_scpi_token_t params[VIPER_SCPI_MAX_PARAMS];
    viper_scpi_token_t prefix[VIPER_SCPI_PREFIX_SIZE]; /**< Relative-path base */
    int32_t suffix[VIPER_SCPI_MAX_SUFFIXES]; /**< -1 = suffix omitted */
    viper_scpi_result_t err_queue[VIPER_SCPI_ERROR_QUEUE_SIZE];
    const char *err_info[VIPER_SCPI_ERROR_QUEUE_SIZE]; /**< Static detail or NULL */
    /* Output queue (message-exchange model): text with at most one generated
     * block spliced in at out_block_at; read order is
     * text[0, block_at), block, text[block_at, out_len). */
    size_t out_len;
    size_t out_rd;
    size_t out_block_at;
    uint32_t out_block_total;
    uint32_t out_block_done;
    viper_scpi_block_fill_t out_fill;
    void *out_fill_user;
    viper_scpi_cmd_callback_t deferred; /**< Callback of the running command,
                                             re-run after viper_scpi_defer() */
    viper_scpi_block_stream_t stream;   /**< Handler of the block being streamed */
    viper_scpi_status_set_t status[2];  /**< OPERation, QUEStionable */
};

#ifdef __cplusplus
}
#endif

#endif /* VIPER_SCPI_TYPES_H */
