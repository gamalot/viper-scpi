# viper-scpi

An ultra-lightweight, zero-allocation, non-blocking SCPI parser for bare-metal
microcontrollers, written in ISO C99. It is transport agnostic: feed it bytes
from UART, USBTMC or a TCP socket, and it dispatches commands to your callbacks
and writes responses through a function pointer you provide.

- No `malloc`/`free`, no RTOS, no blocking or polling.
- Command tree lives in `const` data (Flash); the parser context is one static
  struct (about 330 bytes with the default limits, well under 2 KB).
- Builds warning-free with `-Wall -Wextra -Wpedantic` (GCC and Clang).

## Features

- Hierarchical commands (`:MEASure:VOLTage:DC?`), case-insensitive, short and
  long forms (`MEAS` / `MEASURE`), leading `:` for absolute paths.
- Multiple commands per line separated by `;`, with SCPI relative-path rules:
  `SOUR:VOLT 1;CURR 2` resolves `CURR` under `SOURce`; a leading `:` returns to
  the root; common commands (`*...`) do not change the current path.
- Parameter helpers: integers (decimal, `0x` / `#H` hex, `#B` binary, `#Q`
  octal), floating point with exponent, booleans (`ON`/`OFF`/`1`/`0`), keyword
  choices (short/long form), quoted strings (`"..."` / `'...'`, doubled quotes
  as escape).
- Optional nodes (`"[LEVel]"`): `SOUR:VOLT` and `SOUR:VOLT:LEV` reach the same
  callback; optional intermediate nodes (`"[SENSe]"`) may be omitted too.
- Numeric suffixes (`"CHANnel#"`): `CHAN`, `CHAN2` and `CHANNEL12` match; read
  the number with `viper_scpi_suffix()`.
- `MINimum` / `MAXimum` / `DEFault` numeric keywords with range checking.
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

## Optional nodes and numeric suffixes

```c
static const viper_scpi_node_t bank_nodes[] = { { "ID", cmd_id, NULL }, VIPER_SCPI_NODE_END };
static const viper_scpi_node_t chan_nodes[] = {
    { "VOLTage", cmd_volt, NULL },
    { "[BANK#]", NULL, bank_nodes },       /* optional and suffixed */
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t root[] = {
    { "[SENSe]", NULL, sense_nodes },      /* SENS:TEMP? == TEMP? */
    { "CHANnel#", NULL, chan_nodes },      /* CHAN2:VOLT? */
    VIPER_SCPI_NODE_END
};
```

- A `[NODE]` may be left out of the header. Nodes that have children and no
  callback also resolve to an omittable child that has a callback, so with
  `POWer` -> `[LEVel]`, `POW?` equals `POW:LEV?`. Relative paths follow what
  was written: after `POW?` the path stays at the root.
- `viper_scpi_suffix(ctx, i, default)` returns the suffix of the i-th `#` node
  in the matched path, counting from the left. Omitted suffixes, and skipped
  optional `#` nodes, keep their position and yield `default`
  (`CHAN:VOLT?` -> `viper_scpi_suffix(ctx, 0, 1) == 1`). Indexes count within
  the header being executed, not the inherited relative path. Suffixes have at
  most 9 digits; up to `VIPER_SCPI_MAX_SUFFIXES` per header are recorded.

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

- Optional nodes are declared per node (`"[NAME]"`); the SCPI notation
  `A[:B]:C` inside a single pattern string is not parsed.
- `UP`/`DOWN` and `INFinity`/`NAN` numeric keywords are not implemented.
- Floating point I/O uses `strtod`/`snprintf` from the C library.
- Commands execute synchronously, so `*WAI` and `*OPC` complete immediately.

## License

See `LICENSE`.
