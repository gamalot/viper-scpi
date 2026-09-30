# viper-scpi

An ultra-lightweight, zero-allocation, non-blocking SCPI parser for bare-metal
microcontrollers, written in ISO C99. It is transport agnostic: feed it bytes
from UART, USBTMC or a TCP socket, and it dispatches commands to your callbacks
and writes responses through a function pointer you provide.

- No `malloc`/`free`, no RTOS, no blocking or polling.
- Command tree lives in `const` data (Flash); the parser context is one static
  struct (about 470 bytes with the default limits, well under 2 KB).
- Builds warning-free with `-Wall -Wextra -Wpedantic` (GCC and Clang).

## Features

- Hierarchical commands (`:MEASure:VOLTage:DC?`), case-insensitive, short and
  long forms (`MEAS` / `MEASURE`), leading `:` for absolute paths.
- Multiple commands per line separated by `;`, with SCPI relative-path rules:
  a header without a leading `:` extends the previous header minus its last
  mnemonic, so `SOUR:VOLT 1;CURR 2` resolves `CURR` as `SOUR:CURR`; a leading
  `:` returns to the root; common commands (`*...`) do not change the path.
- Parameter helpers: integers (decimal, `0x` / `#H` hex, `#B` binary, `#Q`
  octal), floating point with exponent, booleans (`ON`/`OFF`/`1`/`0`), keyword
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
  `*STB?`, `*ESR?`, plus `*ESE`, `*ESE?`, `*SRE`, `*SRE?`, `*TST?`.
- SCPI error queue with `SYSTem:ERRor[:NEXT]?`, `SYSTem:ERRor:COUNt?` and
  `SYSTem:VERSion?` built in.

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
- An indefinite block (`#0`) runs to the end of the line and must be the last
  item; a `\r` before the final `\n` is treated as data.
- `#B`, `#Q` and `#H` numbers are not blocks.

## Callbacks

A callback runs when its node is the last mnemonic of a command. Inside it:

- `viper_scpi_is_query(ctx)` tells `CMD?` from `CMD`.
- `viper_scpi_param_*` read parameters by zero-based index. They return
  `VIPER_SCPI_RES_OK` or an SCPI error code (`-104` data type, `-109` missing
  parameter, `-222` out of range, `-224` illegal value, ...).
- `viper_scpi_result_*` emit response values. Values of one query are joined
  with `,`; responses of different commands on a line with `;`; the line ends
  with `\n`.
- Return a non-zero `viper_scpi_result_t` to push that error onto the queue.
  Use `viper_scpi_error_push()` for additional errors.

Callbacks must not call the `viper_scpi_input_*` functions.

## Input handling

`viper_scpi_input_char()` / `viper_scpi_input_buffer()` accumulate a line and
execute it on `\n` (`\r` is ignored). For transports with explicit message
boundaries (USBTMC EOM) call `viper_scpi_end_of_message()`. A line longer than
`VIPER_SCPI_BUFFER_SIZE` is discarded and reports `-363` (input buffer overrun).

## Configuration

Override any of these with `-D` on the compiler command line (see
`include/viper_scpi/scpi_config.h`):

| Macro | Default | Meaning |
| --- | --- | --- |
| `VIPER_SCPI_BUFFER_SIZE` | 128 | Line buffer size in bytes |
| `VIPER_SCPI_MAX_PARAMS` | 8 | Parameters per command |
| `VIPER_SCPI_MAX_RECURSION_DEPTH` | 8 | Mnemonics per header |
| `VIPER_SCPI_ERROR_QUEUE_SIZE` | 8 | Error queue entries |

## Build and test

```sh
cmake -S . -B build -DVIPER_SCPI_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Options: `VIPER_SCPI_BUILD_TESTS`, `VIPER_SCPI_BUILD_EXAMPLES`,
`VIPER_SCPI_WERROR` (all `ON` except the last). To use the library from another
CMake project, `add_subdirectory(viper-scpi)` and link `viper_scpi`.

## Limitations

- Optional brackets in patterns do not nest (`[:A[:B]]`).
- `UP`/`DOWN` and `INFinity`/`NAN` numeric keywords are not implemented.
- Unit conversion is multiplicative only (no offsets such as degF).
- Floating point I/O uses `strtod`/`snprintf` from the C library.
- Commands execute synchronously, so `*WAI` and `*OPC` complete immediately.

## License

See `LICENSE`.
