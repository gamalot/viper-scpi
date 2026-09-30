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

- Optional nodes (`[:NODE]`), numeric suffixes (`CHANnel1`), block data and
  `MIN`/`MAX`/`DEF` numeric keywords are not implemented.
- Floating point I/O uses `strtod`/`snprintf` from the C library.
- Commands execute synchronously, so `*WAI` and `*OPC` complete immediately.

## License

See `LICENSE`.
