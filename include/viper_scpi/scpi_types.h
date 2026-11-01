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

/** Result codes. Negative values are IEEE 488.2 / SCPI error numbers. */
typedef enum {
    VIPER_SCPI_RES_OK = 0,
    VIPER_SCPI_RES_ERR_UNKNOWN_CMD = -100,      /**< Command error */
    VIPER_SCPI_RES_ERR_SYNTAX = -102,           /**< Syntax error */
    VIPER_SCPI_RES_ERR_DATA_TYPE = -104,        /**< Data type error */
    VIPER_SCPI_RES_ERR_PARAM_NOT_ALLOWED = -108,/**< Parameter not allowed */
    VIPER_SCPI_RES_ERR_MISSING_PARAM = -109,    /**< Missing parameter */
    VIPER_SCPI_RES_ERR_MNEMONIC_TOO_LONG = -112,/**< Program mnemonic too long */
    VIPER_SCPI_RES_ERR_UNDEFINED_HEADER = -113, /**< Undefined header */
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
    VIPER_SCPI_RES_ERR_QUERY_UNTERMINATED = -420/**< Read requested, nothing to send */
} viper_scpi_result_t;

/** Event Status Register bits (IEEE 488.2). */
enum {
    VIPER_SCPI_ESR_OPC = 0x01, /**< Operation complete */
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

/** Command callback. A non-zero return value is pushed to the error queue. */
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

/** Unit for viper_scpi_param_double_unit(); tables end with a NULL name. */
typedef struct {
    const char *name;  /**< Case-insensitive, e.g. "HZ" */
    double scale;      /**< Factor to the base unit (1.0 unless e.g. MIN = 60) */
    uint8_t flags;     /**< VIPER_SCPI_UNIT_* */
} viper_scpi_unit_t;

/** Keyword/value pair for viper_scpi_param_choice(); lists end with NULL name. */
typedef struct {
    const char *name;
    int value;
} viper_scpi_choice_t;

/** Output function: send @p len bytes to the transport. */
typedef size_t (*viper_scpi_write_t)(void *user_data, const char *data, size_t len);

/** Optional *RST / *TRG hook. */
typedef void (*viper_scpi_reset_t)(viper_scpi_context_t *ctx);
typedef void (*viper_scpi_trigger_t)(viper_scpi_context_t *ctx);

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
 * pushed to the error queue; after one from BEGIN or DATA the rest of the
 * block is skipped and the handler is not called again for it. ABORT's return
 * value is ignored.
 */
typedef viper_scpi_result_t (*viper_scpi_block_stream_t)(viper_scpi_context_t *ctx,
                                                         viper_scpi_stream_event_t event,
                                                         const uint8_t *data, size_t len);

/** Opt-in of a tree node to streaming block input; lists end with a NULL node. */
typedef struct {
    const viper_scpi_node_t *node;       /**< Address of the node in the tree */
    viper_scpi_block_stream_t handler;
} viper_scpi_stream_node_t;

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

/** Parameter token: points into the line buffer, not NUL terminated. */
typedef struct {
    const char *str;
    size_t len;
} viper_scpi_token_t;

/**
 * Parser context. Allocate it statically; treat all fields as private and
 * use the accessor functions in viper_scpi.h.
 */
struct viper_scpi_context_s {
    const viper_scpi_config_t *cfg;
    void *user_data;
    char buffer[VIPER_SCPI_BUFFER_SIZE];
    size_t length;
    viper_scpi_token_t prefix[VIPER_SCPI_MAX_RECURSION_DEPTH]; /**< Relative-path base */
    viper_scpi_token_t params[VIPER_SCPI_MAX_PARAMS];
    size_t param_count;
    int16_t err_queue[VIPER_SCPI_ERROR_QUEUE_SIZE];
    int32_t suffix[VIPER_SCPI_MAX_SUFFIXES]; /**< -1 = suffix omitted */
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
    uint8_t out_block_active;
    uint8_t out_overflow;
    uint8_t srq_prev;      /**< Service-request summary at the last check */
    uint8_t srq_new;       /**< Rising edge not yet taken */
    uint8_t rqs;           /**< Service requested and not yet polled */
    /* Overlapped commands: a message suspended by *WAI or *OPC? resumes at
     * exec_pos once no operation is pending. */
    size_t exec_pos;
    uint8_t op_count;      /**< Pending operations (op_begin - op_end) */
    uint8_t wait;          /**< WAIT_* : why execution is suspended */
    uint8_t opc_armed;     /**< *OPC given while operations were pending */
    uint8_t input_dropped; /**< Bytes arrived while suspended */
    viper_scpi_status_set_t status[2]; /**< OPERation, QUEStionable */
    /* Streaming block input */
    viper_scpi_block_stream_t stream;  /**< Handler of the block being streamed */
    size_t unit_start;     /**< Buffer offset where the current unit starts */
    size_t hash_pos;       /**< Buffer offset of the '#' of the current block */
    uint8_t msg_started;   /**< Part of this message has already executed */
    uint32_t block_remaining;
    uint8_t suffix_count;
    uint8_t prefix_count;
    uint8_t block_state;
    uint8_t block_digits;
    char quote;
    char last;
    uint8_t err_head;
    uint8_t err_count;
    uint8_t overflow;
    uint8_t is_query;
    uint8_t line_has_output;
    uint8_t unit_has_output;
    uint8_t esr;
    uint8_t ese;
    uint8_t sre;
};

#ifdef __cplusplus
}
#endif

#endif /* VIPER_SCPI_TYPES_H */
