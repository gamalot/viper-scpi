# viper-scpi

[![CI](https://github.com/gamalot/viper-scpi/actions/workflows/ci.yml/badge.svg)](https://github.com/gamalot/viper-scpi/actions/workflows/ci.yml)

An ultra-lightweight, zero-allocation, non-blocking SCPI parser for bare-metal
microcontrollers, written in ISO C99. It is transport agnostic: feed it bytes
from UART, USBTMC or a TCP socket, and it dispatches commands to your callbacks
and writes responses through a function pointer you provide.

- No `malloc`/`free`, no RTOS, no blocking or polling.
- Command tree lives in `const` data (Flash); the parser context is one static
  struct (about 470 bytes with the default limits, well under 2 KB).
- Builds warning-free with `-Wall -Wextra -Wpedantic` (GCC and Clang).

**Guides:** [docs/](docs/README.md) -- getting started, the command tree,
parameters, transport integration (UART, USBTMC, VXI-11), status and errors,
overlapped commands, large data, configuration and troubleshooting.

This README, the comments in `include/viper_scpi/viper_scpi.h` and
[CHANGELOG.md](CHANGELOG.md) are the reference. The original generation
prompt is kept, out of date, in [docs/history/](docs/history/).

## Features

- Hierarchical commands (`:MEASure:VOLTage:DC?`), case-insensitive, short and
  long forms (`MEAS` / `MEASURE`), leading `:` for absolute paths.
- Multiple commands per line separated by `;`, with SCPI relative-path rules:
  a header without a leading `:` extends the previous header minus its last
  mnemonic, so `SOUR:VOLT 1;CURR 2` resolves `CURR` as `SOUR:CURR`; a leading
  `:` returns to the root; common commands (`*...`) do not change the path.
- Parameter helpers: integers (decimal numbers such as `1.5` or `3.2E1` are
  rounded, without floating point; `0x` / `#H` hex, `#B` binary, `#Q` octal),
  floating point with exponent, booleans (`ON`/`OFF` or a number: non-zero is
  ON), keyword
  choices (short/long form), quoted strings (`"..."` / `'...'`, doubled quotes
  as escape).
- Optional mnemonics in the SCPI notation: `"VOLTage[:LEVel]"`,
  `"TRIGger[:SEQuence]:SOURce"`, `"[SENSe]"`. Omitted parts may be written or
  left out.
- Numeric suffixes (`"CHANnel#"`): `CHAN`, `CHAN2` and `CHANNEL12` match; read
  the number with `viper_scpi_suffix()`.
- `MINimum` / `MAXimum` / `DEFault` numeric keywords with range checking.
- Numbers with units and multipliers (`1.5KHZ`, `100 mV`, `10MHZ`, `2MIN`),
  driven by unit tables (ready-made ones for V, A, Hz, ohm, W, F, H, dB, s, degC).
- IEEE 488.2 arbitrary block data, definite (`#15hello`) and indefinite
  (`#0...`), as parameters and as responses.
- IEEE 488.2 commands: `*IDN?`, `*RST`, `*CLS`, `*OPC`, `*OPC?`, `*WAI`,
  `*STB?`, `*ESR?`, plus `*ESE`, `*ESE?`, `*SRE`, `*SRE?`, `*TST?` and, with a
  trigger hook, `*TRG`. The ESR reports PON after `viper_scpi_init()`.
- SCPI error queue with `SYSTem:ERRor[:NEXT]?`, `SYSTem:ERRor:COUNt?` and
  `SYSTem:VERSion?` built in.
- SCPI STATus subsystem: OPERation and QUEStionable register sets with
  transition filters, summarised in status byte bits 7 and 3.
- Two output models: streaming to a `write` function (UART, raw TCP), or a
  response queue the transport reads from (USBTMC, VXI-11, HiSLIP, GPIB
  device), with MAV, `-410` Query INTERRUPTED, device clear, service-request
  edges and arbitrarily large generated block responses.

## Quick start

```c
#include "viper_scpi/viper_scpi.h"

static viper_scpi_result_t cmd_volt(viper_scpi_context_t *ctx)
{
    static double volts;
    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_double(ctx, volts);
        return VIPER_SCPI_RES_OK;
    }
    return viper_scpi_param_double(ctx, 0, &volts);   /* error code -> queue */
}

static const viper_scpi_node_t source_nodes[] = {
    { "VOLTage", cmd_volt, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t root[] = {
    { "SOURce", NULL, source_nodes },      /* NULL callback: hierarchy node */
    VIPER_SCPI_NODE_END
};

static size_t uart_write(void *user, const char *data, size_t len) { /* ... */ return len; }

static const viper_scpi_config_t cfg = {
    .root = root, .write = uart_write,
    .idn_manufacturer = "Acme", .idn_model = "PSU", .idn_serial = "1", .idn_firmware = "1.0"
};
static viper_scpi_context_t scpi;

void app_init(void)        { viper_scpi_init(&scpi, &cfg); }
void uart_rx_byte(char c)  { viper_scpi_input_char(&scpi, c); }
```

Use designated initializers for `viper_scpi_config_t`, as above: fields are
added at the end over time, and a positional initializer then trips
`-Wmissing-field-initializers`.

## Message-exchange transports (USBTMC, VXI-11, HiSLIP, GPIB device)

On these transports the controller *asks* for a reply, possibly in several
pieces, and IEEE 488.2 attaches rules to the reply that sits waiting. Give the
configuration an output buffer and the library keeps the reply instead of
writing it:

```c
static char reply[512];                          /* one response message */
static const viper_scpi_config_t cfg = {
    .root = root, .output_buffer = reply, .output_size = sizeof reply,
    .trigger = on_trigger,                       /* enables *TRG (optional) */
    /* idn_... */
};
```

| Transport event | Call |
| --- | --- |
| message bytes arrive | `viper_scpi_input_buffer()` |
| end of message (USBTMC EOM, VXI-11 END) | `viper_scpi_end_of_message()` |
| controller requests a reply of up to *n* bytes | `viper_scpi_output_read(ctx, buf, n, &end)`; send `end` as EOM/END |
| request with nothing queued | while `viper_scpi_busy(ctx)`, keep the request and answer it once `viper_scpi_output_pending()` (the reply of `INIT;*OPC?` is still coming); otherwise push `VIPER_SCPI_RES_ERR_QUERY_UNTERMINATED` (-420) and let the controller time out |
| abort of a reply (USBTMC ABORT_BULK_IN) | `viper_scpi_output_discard()` |
| abort of incoming data only (USBTMC ABORT_BULK_OUT, CLEAR_FEATURE on bulk OUT, VXI-11 abort) | `viper_scpi_input_discard()`: the queued reply stays readable |
| device clear (viClear, USBTMC INITIATE_CLEAR, VXI-11 device_clear) | `viper_scpi_device_clear()` |
| main loop | `viper_scpi_poll(ctx)`; `if (viper_scpi_take_service_request(ctx))` raise SRQ |
| before accepting more bytes | hold off while `viper_scpi_busy(ctx)` (overlapped commands) |
| serial poll / READ_STATUS_BYTE / device_readstb | `viper_scpi_serial_poll()`: bit 6 is RQS, cleared by the poll |
| SRQ notification that carries the status byte (USB488 interrupt-IN) | `viper_scpi_rqs_delivered()` after sending it: that notification *is* the poll |

What changes in this mode:

- The status byte carries **MAV** (bit 4) while a reply is queued.
- A new program message arriving over an unread (or partly read) reply
  discards it and queues **-410** Query INTERRUPTED (488.2 6.3.2.3). A line of
  only white space is not a message.
- A newline inside an open string quote is string data, since the message
  end is signalled by EOM/END and IEEE 488.2 7.7.5 allows any 7-bit ASCII
  character in string data: `STR "a<LF>b"` sets `a<LF>b`. Likewise a LF inside
  an indefinite `#0` block is block data; the block ends at EOM (NL^END).
  In the stream model LF always ends the message, even inside quotes, so one
  lost quote cannot swallow every message after it.
- A reply that does not fit `output_size` is dropped whole with **-321** Out of
  memory, rather than handed out truncated.
- `viper_scpi_result_block_fill(ctx, len, fill, user)` adds a definite-length
  block whose bytes `fill` produces while the transport reads them, so a
  reply can be megabytes long from a small buffer. One such block per reply
  (a second gives -400); text before and after it is kept in order. In the
  stream model the same call generates and writes the block in 64-byte chunks.
- `viper_scpi_take_service_request()` reports each false-to-true transition
  of (STB & SRE) once. Transitions are tracked as they happen, so
  `*CLS;*SRE 32;*OPC` on one line is a new request even though the condition
  holds before and after the line -- sampling the status byte between
  messages would miss it.

`viper_scpi_device_clear()` drops a partly received message, the
compound-header path and the queued reply; the status registers and the error
queue are kept, as 488.2 requires of DCL/SDC.

## SCPI STATus subsystem

Built in (a user tree may override it): `STATus:OPERation[:EVENt]?`,
`:CONDition?`, `:ENABle[?]`, `:PTRansition[?]`, `:NTRansition[?]`, the same
under `STATus:QUEStionable`, and `STATus:PRESet`. Registers are 16 bits with
bit 15 always 0, so writable values are 0..32767 (`-222` otherwise).

The instrument reports its state through the condition registers:

```c
/* OPERation bit 4 = MEASuring, QUEStionable bit 1 = CURRent, per SCPI-99 */
viper_scpi_status_condition(ctx, VIPER_SCPI_STATUS_OPER, 1u << 4, measuring ? 1u << 4 : 0);
viper_scpi_status_condition(ctx, VIPER_SCPI_STATUS_QUES, 1u << 1, overload ? 1u << 1 : 0);
```

A 0 -> 1 change passes `PTRansition`, a 1 -> 0 change `NTRansition`, into the
event register, which latches until read (`STAT:OPER?`) or `*CLS`. Enabled
event bits set the summary in the status byte -- OPER is bit 7, QUES bit 3 --
and therefore take part in service requests (`*SRE 8` requests service on a
QUES event). Power-on and `STAT:PRES` set enables to 0, PTR to all ones and NTR
to 0; neither touches events or conditions. `*CLS` clears both event registers.

## Overlapped commands: `*OPC`, `*OPC?`, `*WAI`

A command that starts a slow operation (a measurement, a relay settling, a
sweep) need not block. Its callback calls `viper_scpi_op_begin(ctx)` and
returns; the application calls `viper_scpi_op_end(ctx)` when the operation
finishes. Operations are counted, so several may overlap.

| While an operation is pending | Behaviour |
| --- | --- |
| `*OPC` | arms; OPC is set in the ESR by the last `op_end()`, which can raise a service request edge |
| `*OPC?` | the rest of the message is suspended; the `1` is produced on completion |
| `*WAI` | the rest of the message is suspended until completion |
| no operation pending | all three complete at once, as before |

Suspension never blocks. The unexecuted units stay in the line buffer, and
`viper_scpi_poll(ctx)`, called from the main loop, resumes them once no
operation is pending. Meanwhile:

- `viper_scpi_busy(ctx)` is true and input is refused: the transport holds the
  controller off (USBTMC NAKs bulk-OUT, a UART deasserts RTS or buffers).
  Bytes fed anyway are refused: the message they belong to is dropped whole
  (also if the suspension ends before its last byte arrives) and reported as
  `-363` at its end. `viper_scpi_input_buffer()` refuses the rest of its data
  once a message in it suspends; `viper_scpi_input_until_busy()` stops right
  there instead and returns how many bytes it consumed, for a transport that
  can keep the rest (pipelined commands in one TCP segment).
- In the message-exchange model the response is not readable (MAV clear,
  `viper_scpi_output_read()` returns nothing) until the message finishes; in
  the stream model responses already produced have been written.
- `viper_scpi_device_clear()` cancels the suspended message and an armed
  `*OPC` or waiting `*OPC?` (IEEE 488.2 Operation Complete Command/Query Idle
  States). `*CLS` and `*RST` also disarm `*OPC`. The operations themselves
  belong to the application and are not touched.

A query that needs time itself (`MEAS:CURR?` averaging an ADC) can answer
later instead of blocking: its callback calls `viper_scpi_op_begin()` and
`viper_scpi_defer()` and returns. The message is suspended as at `*WAI`; once
no operation is pending, `viper_scpi_poll()` calls the same callback again,
with its parameters and suffixes and `viper_scpi_resumed(ctx)` true, and that
call writes the result.

Call these functions from the same context as the input functions, not from
an interrupt; an ISR should set a flag that the main loop turns into
`viper_scpi_op_end()`.

## Command tree registration

A tree is a `const` array of `viper_scpi_node_t { pattern, callback, children }`
terminated by `VIPER_SCPI_NODE_END`.

- `pattern` uses the SCPI convention: the leading run of upper-case letters is
  the short form and the whole string the long form. `"VOLTage"` matches
  `VOLT` and `VOLTAGE` in any case, but not `VOL` or `VOLTA`.
- `callback` is `NULL` for intermediate nodes. A node may have both a callback
  and children (as `SYSTem:ERRor` does).
- `children` is `NULL` for leaves.

## Node patterns: optional mnemonics and numeric suffixes

A node `pattern` is a path of one or more mnemonics separated by `:`.

```c
static const viper_scpi_node_t root[] = {
    { "TRIGger[:SEQuence]:SOURce", cmd_trig_src, NULL },  /* TRIG:SOUR, TRIG:SEQ:SOUR */
    { "SOURce", NULL, source_nodes },
    { "[SENSe]", NULL, sense_nodes },                     /* SENS:TEMP? == TEMP? */
    { "CHANnel#", NULL, chan_nodes },                     /* CHAN2:VOLT? */
    { "CALCulate#[:MATH]:STATe", cmd_calc_state, NULL },  /* CALC2:MATH:STAT? */
    VIPER_SCPI_NODE_END
};
```

- `[:NAME]` or `[NAME]` marks a mnemonic that may be left out; brackets do not
  nest. Mandatory mnemonics must be present. A node whose mnemonics are all
  optional can vanish from the header, so with `POWer` -> `[LEVel]` a bare
  `POW?` runs the `LEVel` callback.
- Children continue after the whole pattern: with
  `{ "ROUTe:LAYer[:SUB]", cb, layer_nodes }`, both `ROUT:LAY:COUN?` and
  `ROUT:LAY:SUB:COUN?` reach `COUNt` in `layer_nodes`.
- A malformed pattern (for example an unclosed bracket) never matches.
- `NAME#` accepts a numeric suffix. `viper_scpi_suffix(ctx, i, default)` returns
  the suffix of the i-th `#` mnemonic of the full path, counted from the left.
  Omitted suffixes, and skipped optional `#` mnemonics, keep their position and
  yield `default` (`CHAN:VOLT?` gives `viper_scpi_suffix(ctx, 0, 1) == 1`).
  Suffixes have at most 9 digits; up to `VIPER_SCPI_MAX_SUFFIXES` are recorded
  per command.
- The path includes the inherited prefix: in `CHAN5:VOLT?;RANG6?` the second
  command sees suffixes 5 and 6.

## Numbers with units

```c
double hz;
res = viper_scpi_param_double_unit(ctx, 0, viper_scpi_units_hertz, &hz);
res = viper_scpi_param_double_unit_kw(ctx, 0, viper_scpi_units_volt,
                                      0.0, 30.0, 5.0, &volts);   /* + MIN/MAX/DEF */
```

The result is in base units (`1.5KHZ` gives 1500.0). A bare number is accepted
as well. Multipliers are `EX PE T G MA K M U N P F A` (case-insensitive):
`MA` is mega and `M` is milli (`10MA` on amps is 10 mA), except for units with
`VIPER_SCPI_UNIT_M_IS_MEGA`, where `MHZ` and `MOHM` mean mega as the standard
requires. A suffix that is not in the table, or a prefix on a unit that forbids
it, returns `-131` (invalid suffix). Use `1E-3` for milliohm.

Tables are `{ name, scale, flags }` arrays ending with a NULL name, so you can
define your own:

```c
static const viper_scpi_unit_t pressure[] = {
    { "PA", 1.0, 0 },                                   /* KPA, MPA, ... */
    { "BAR", 1e5, VIPER_SCPI_UNIT_NO_PREFIX },
    { NULL, 0, 0 }
};
```

Ready-made tables: `viper_scpi_units_volt`, `_amp`, `_hertz`, `_ohm`, `_watt`,
`_farad`, `_henry`, `_decibel`, `_second` (S, SEC, MIN, HOUR) and `_celsius`.
The plain `viper_scpi_param_double()` still rejects units.

## MIN / MAX / DEF

```c
double v;
res = viper_scpi_param_double_kw(ctx, 0, /*min*/ 0.0, /*max*/ 30.0, /*def*/ 5.0, &v);
```

`MIN`, `MAX`, `DEF` (or `MINIMUM`, `MAXIMUM`, `DEFAULT`) resolve to the given
limits; numbers must be inside `[min, max]` or `-222` is returned. Queries such
as `VOLT? MAX` can use the same call. `viper_scpi_param_int32_kw()` does the
same for integers.

All double parsers also accept `INFinity`, `NINFinity` (negative infinity) and
`NAN`. In the range-checked ones an infinity must lie within `[min, max]` (so
it fails a finite range with `-222`) and `NAN` is always `-224`.

`UP` and `DOWN` are for instruments that step a setting. The `_kw_ex`
variants accept them and report which keyword was given:

```c
viper_scpi_numeric_kw_t kw;
res = viper_scpi_param_double_kw_ex(ctx, 0, 0.0, 30.0, 5.0, &volts, &kw);
if (res == VIPER_SCPI_RES_OK && kw == VIPER_SCPI_KW_UP)   volts += step;   /* then range-check */
if (res == VIPER_SCPI_RES_OK && kw == VIPER_SCPI_KW_DOWN) volts -= step;
```

`out` is left unchanged for `UP`/`DOWN`. There are `_ex` variants of the
int32 and unit parsers too. The plain `_kw` parsers still reject `UP`/`DOWN`.

## Block data

```c
const uint8_t *data; size_t len;
if (viper_scpi_param_block(ctx, 0, &data, &len) == VIPER_SCPI_RES_OK) { /* ... */ }
viper_scpi_result_block(ctx, buf, buf_len);   /* emits "#<n><length><bytes>" */
```

Block bytes may be any value, including `\n`, `;`, `,` and quotes, so the input
side tracks blocks while assembling a line: a `#` that starts a parameter and
is followed by a digit opens a block. Consequences:

- The whole block must fit in `VIPER_SCPI_BUFFER_SIZE` together with the rest of
  the line, otherwise the line is dropped with `-363`. The declared length is
  still consumed, so the parser stays in sync.
- A definite block consumes exactly its declared number of bytes; an incorrect
  (too large) length swallows following bytes until `viper_scpi_end_of_message()`
  is called.
- If the message ends (`viper_scpi_end_of_message()`, or LF on a stream
  transport) before a block is complete -- right after its `#`, inside its
  length digits or its data -- the whole message is dropped with **-161**
  Invalid block data; no command of it runs.
- A `#` followed by `;`, `,` or whitespace begins neither a block nor a `#H`/
  `#Q`/`#B` number, so that command fails with **-161** too (the other commands
  of the message run). `#` followed by a letter still reaches the callback.
- An indefinite block (`#0`) runs to the end of the line and must be the last
  item; a `\r` before the final `\n` is treated as data.
- `#B`, `#Q` and `#H` numbers are not blocks.

### Streaming block input (uploads larger than RAM)

A node can receive its definite-length block in chunks instead of through the
line buffer, for `MMEM:DATA`, `TRAC:DATA`, firmware images and the like. List
it in the configuration's `stream_nodes` table, by node address:

```c
static const viper_scpi_node_t mmem_nodes[] = {
    { "DATA", cmd_mmem_data, NULL },      /* still used for queries, #0 blocks */
    VIPER_SCPI_NODE_END
};
static const viper_scpi_stream_node_t streams[] = {
    { &mmem_nodes[0], mmem_data_stream },
    { NULL, NULL }
};
/* config: .stream_nodes = streams */

static viper_scpi_result_t mmem_data_stream(viper_scpi_context_t *ctx,
                                            viper_scpi_stream_event_t ev,
                                            const uint8_t *data, size_t len)
{
    switch (ev) {
    case VIPER_SCPI_STREAM_BEGIN: /* len = declared size; params before the block readable */
    case VIPER_SCPI_STREAM_DATA:  /* len bytes at data, as they arrive */
    case VIPER_SCPI_STREAM_END:   /* all bytes received, command complete */
    case VIPER_SCPI_STREAM_ABORT: /* cut short: discard what was written */
    }
    return VIPER_SCPI_RES_OK;
}
```

When the length of a block has been read, the library resolves the command
right away, before any data arrives, and passes the data on as it comes
(`viper_scpi_input_buffer()` hands over whole runs, so chunks are as large as
the transport's packets). The rules:

- Only a definite-length block of the **first command of a program message**
  streams; parameters before it (`MMEM:DATA 'file.bin',#9...`) are readable at
  BEGIN. Elsewhere, indefinite blocks and nodes not in the table use the line
  buffer as before (a block that does not fit is still `-363`).
- Commands after the streamed one in the same message run normally, with its
  path like after any command (`MMEM:DATA ...;DATA ...` is `MMEM:DATA` twice;
  only the first streams).
- End of message before the declared length: ABORT, then `-161`.
  `viper_scpi_device_clear()` and `viper_scpi_input_discard()` mid-block:
  ABORT, no error.
- A parameter after the block: ABORT, `-108`, and the rest of that command is
  skipped -- strings and blocks in it included, so a `;` inside them does not
  start a command; later commands still run.
- A non-zero return from BEGIN or DATA is queued and the rest of the block is
  skipped without further calls. END may write responses; in the
  message-exchange model they are readable once the whole message is done.

## Callbacks

A callback runs when its node is the last mnemonic of a command. Inside it:

- `viper_scpi_is_query(ctx)` tells `CMD?` from `CMD`.
- `viper_scpi_expect(ctx, set_params, query_params)` is the usual first line:
  it returns `-113` for a form the command does not have
  (`VIPER_SCPI_NO_FORM`) and `-108` for surplus parameters, so
  `viper_scpi_expect(ctx, 1, 0)` accepts `VOLT 5` and `VOLT?` only.
- `viper_scpi_param_*` read parameters by zero-based index. They return
  `VIPER_SCPI_RES_OK` or an SCPI error code (`-104` data type, `-109` missing
  parameter, `-222` out of range, `-224` illegal value, ...).
- `viper_scpi_result_double()` writes NR2/NR3 with `VIPER_SCPI_DOUBLE_DIGITS`
  (15) significant digits and an upper-case exponent (`1.5E-07`), as IEEE
  488.2 8.7.4 requires; `viper_scpi_result_double_digits()` picks the digits
  per call (e.g. 7 for a 6.5-digit meter). 15 digits return any value written
  with up to 15 digits unchanged; 17 round-trips every double.
- `viper_scpi_result_*` emit response values. Values of one query are joined
  with `,`; responses of different commands on a line with `;`; the line ends
  with `\n`.
- Return a non-zero `viper_scpi_result_t` to push that error onto the queue.
  `return viper_scpi_fail(ctx, code, "detail");` queues a code (device codes
  such as 101 too) with detail text, once. Use `viper_scpi_error_push()` for
  additional errors.

## Error texts and detail

Give the configuration a table for the instrument's own error numbers (and,
if wanted, different texts for standard ones); it is searched before the
built-in texts, which only the codes it lists are shadowed for:

```c
static const viper_scpi_error_text_t my_errors[] = {
    { 101, "Relay stuck" },
    { 0, NULL }
};
/* config: .error_texts = my_errors */
```

`viper_scpi_error_push_info(ctx, -222, "VOLT 31 exceeds 30")` adds detail that
`SYSTem:ERRor?` appends as SCPI-99 allows: `-222,"Data out of range;VOLT 31
exceeds 30"`. The string is not copied -- pass a literal or other static
storage. Quotes in texts and detail are doubled in the response. When the queue
overflows, the `-350` entry carries no detail. `viper_scpi_error_pop_info()`
returns the detail to C code; `viper_scpi_error_text()` looks up a text the way
`SYSTem:ERRor?` does.

Callbacks must not call the `viper_scpi_input_*` functions.

## Common commands

The IEEE 488.2 commands are built in (`*IDN?`, `*RST`, `*CLS`, `*OPC`,
`*OPC?`, `*WAI`, `*STB?`, `*ESR?`, `*ESE`, `*SRE`, `*TST?`, `*TRG`). Hooks in
the configuration connect them to the instrument: `reset` (`*RST`), `trigger`
(`*TRG`), `clear` (`*CLS`, for device-specific event registers) and
`self_test` (`*TST?`, which otherwise answers 0). The instrument's own common
commands go in `cfg.common`, a node list with the names without `*`
(`{ "SAV", cmd_sav, NULL }`); it is searched before the built-in ones, so an
entry there also replaces one.

## Input handling

`viper_scpi_init()` refuses (`-220`, context untouched) a context whose size
differs from the library's -- the library and the application were built with
different `VIPER_SCPI_*` limits. Define the limits identically for both. (It
is a macro passing `sizeof(viper_scpi_context_t)` to
`viper_scpi_init_sized()`; `(viper_scpi_init)(ctx, cfg)` is the plain
function.)

`viper_scpi_input_char()` / `viper_scpi_input_buffer()` accumulate a line and
execute it on `\n`. CR, NUL, tab and the other control bytes are white space
(IEEE 488.2 7.4.1.2), so `\r\n` ends a line too and a stray CR separates
tokens rather than joining them. For transports with explicit message
boundaries (USBTMC EOM) call `viper_scpi_end_of_message()`. A line longer than
`VIPER_SCPI_BUFFER_SIZE` is discarded and reports `-363` (input buffer overrun).

## Leniency options

Some host software is not standard-conformant. Two workarounds can be switched
on in `viper_scpi_config_t.options`; **the default (0) is the standard
behaviour**, and each option changes only what it names.

| Option | Effect |
| --- | --- |
| `VIPER_SCPI_OPT_RELATIVE_FALLBACK` | A relative header that does not resolve against the current path is retried from the root. `SOUR:VOLT 1;SOUR:CURR 2` then sets the current; by SCPI-99 6.2.4 the second unit is `SOUR:SOUR:CURR` and `-113`. |
| `VIPER_SCPI_OPT_EOM_MESSAGES` | Message-exchange model only: LF separates commands like `;` (each line starts at the root) and only EOM/END ends the program message. `*IDN?<LF>*OPC?<LF>` in one transport message then answers `...;1` instead of interrupting the first reply with `-410`. LFs inside strings and blocks stay data. |

## Configuration

Override any of these with `-D` on the compiler command line (see
`include/viper_scpi/scpi_config.h`):

| Macro | Default | Meaning |
| --- | --- | --- |
| `VIPER_SCPI_BUFFER_SIZE` | 128 | Line buffer size in bytes |
| `VIPER_SCPI_MAX_PARAMS` | 8 | Parameters per command |
| `VIPER_SCPI_MAX_RECURSION_DEPTH` | 8 | Mnemonics per header |
| `VIPER_SCPI_ERROR_QUEUE_SIZE` | 8 | Error queue entries |
| `VIPER_SCPI_DOUBLE_DIGITS` | 15 | Significant digits of double responses |
| `VIPER_SCPI_MAX_SUFFIXES` | 4 | Numeric suffixes recorded per header |

## Versions

`viper_scpi.h` defines `VIPER_SCPI_VERSION_MAJOR`, `_MINOR`, `_PATCH` and
`VIPER_SCPI_VERSION_STRING`; CMake refuses to configure if they disagree with
`project(VERSION)`. Releases are tagged `vX.Y.Z` -- pin a tag rather than a
commit when vendoring -- and [CHANGELOG.md](CHANGELOG.md) lists each release's
changes, with breaking ones marked.

## Build and test

```sh
cmake -S . -B build -DVIPER_SCPI_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Options: `VIPER_SCPI_BUILD_TESTS`, `VIPER_SCPI_BUILD_EXAMPLES` and
`VIPER_SCPI_INSTALL` default to `ON` only when viper-scpi is the top-level
project; `VIPER_SCPI_WERROR` defaults to `OFF`.

To use the library from another CMake project, either vendor it:

```cmake
add_subdirectory(third_party/viper-scpi)     # builds only the library
target_link_libraries(firmware PRIVATE viper_scpi::viper_scpi)
```

or install it (`cmake --install build --prefix <dir>`) and use
`find_package(viper_scpi 0.2 REQUIRED)` with the same target name.

## Continuous integration

`.github/workflows/ci.yml` runs on every push to `main` and on pull requests:

| Job | What it checks |
| --- | --- |
| `build-test` | GCC and Clang, Debug and Release, on Ubuntu (latest and 26.04) and Clang on macOS, `-Werror`, CTest |
| `sanitizers` | Full test suite, including the fuzz smoke test, under ASan and UBSan |
| `strict-c99` | `-std=c99 -pedantic-errors -Wshadow -Wconversion ...`, no `malloc`/`free` references, context size below 2 KB |
| `static-analysis` | `clang-tidy` (`clang-analyzer-*`, `bugprone-*`) on the library |
| `cmake-integration` | `add_subdirectory()` builds only the library; install + `find_package()` |
| `cortex-m` | Cross-compiles for Cortex-M4 and M0+ with `arm-none-eabi-gcc`, prints code size, fails if the library has writable static data, and links an integer-only program that must not contain printf/strtod or 64-bit helpers |

Every Linux job runs on both `ubuntu-latest` and `ubuntu-26.04`, so the move
of `ubuntu-latest` to the newer image is already covered.

The same checks can be run locally; see the commands in the workflow file.

## Limitations

- Optional brackets in patterns do not nest (`[:A[:B]]`).
- Unit conversion is multiplicative only (no offsets such as degF).
- Floating-point I/O uses `strtod`/`snprintf` from the C library. They live
  in `src/scpi_float.c` alone, so firmware that never calls a double API
  (`viper_scpi_param_double*`, `viper_scpi_result_double*`) links neither:
  integer responses are formatted without `printf`. The `cortex-m` CI job
  checks this with an integer-only Cortex-M0+ program (`.github/linktest.sh`).
- In the message-exchange model a reply holds at most one generated block,
  and lines separated by `\n` inside one transport message are separate
  program messages (so a query on the first line is interrupted by the
  second) unless `VIPER_SCPI_OPT_EOM_MESSAGES` is set.

Planned work, with priorities and acceptance criteria, is in
[docs/IMPROVEMENTS.md](docs/IMPROVEMENTS.md).

## License

See `LICENSE`.
