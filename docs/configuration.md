# Configuration and footprint

[Documentation home](README.md)

## Runtime configuration: `viper_scpi_config_t`

One `const` structure per context, which must outlive it. Initialise it with
designated initializers; every field is optional and zero means the default.

| Field | Purpose |
| --- | --- |
| `root` | your command tree |
| `write` | output function (stream model) |
| `user_data` | passed to `write`; `viper_scpi_user_data(ctx)` returns it to callbacks |
| `reset` | called by `*RST` |
| `idn_manufacturer`, `idn_model`, `idn_serial`, `idn_firmware` | the four `*IDN?` fields |
| `output_buffer`, `output_size` | response queue; selects the message-exchange model |
| `trigger` | called by `*TRG`; without it `*TRG` is -113 |
| `stream_nodes` | streaming block input, see [Large data](large-data.md) |
| `error_texts` | your error texts, see [Status and errors](status-and-errors.md) |
| `options` | leniencies, below |
| `common` | your own common commands (`*SAV`, `*OPT?`, ...), see [Command tree](command-tree.md#common-commands) |
| `clear` | called by `*CLS` after the library's status data is cleared |
| `self_test` | `*TST?` answers its result; without it `*TST?` answers 0 |

Several instruments or interfaces in one firmware (a USBTMC and a UART port,
say) each get their own context and configuration; trees and tables can be
shared.

## Leniency options

`options` is a bit set; **0 is standard behaviour**.

| Option | Effect | When to use |
| --- | --- | --- |
| `VIPER_SCPI_OPT_RELATIVE_FALLBACK` | a relative header that does not resolve against the current path is retried from the root, so `SOUR:VOLT 1;SOUR:CURR 2` works | host software that repeats the full path after `;` |
| `VIPER_SCPI_OPT_EOM_MESSAGES` | message-exchange model only: LF separates commands like `;` and only EOM ends the message | hosts that send several newline-terminated commands in one transfer and expect one combined reply |

## Compile-time limits

Override with `-D` for the library **and** every file that includes its
headers (they change the size of the context). A value outside the range
shown is a compile error, and `viper_scpi_init()` returns -220 without
touching the context if the application's context size differs from the
library's.

| Macro | Default | Meaning |
| --- | --- | --- |
| `VIPER_SCPI_BUFFER_SIZE` | 128 | bytes of one program message, blocks included |
| `VIPER_SCPI_MAX_PARAMS` | 8 | parameters per command |
| `VIPER_SCPI_MAX_RECURSION_DEPTH` | 8 | mnemonics per header, including the inherited path (1..255) |
| `VIPER_SCPI_MAX_SUFFIXES` | 4 | numeric suffixes recorded per command, 1..255 (later ones read as the default) |
| `VIPER_SCPI_ERROR_QUEUE_SIZE` | 8 | error queue entries (2..255) |
| `VIPER_SCPI_DOUBLE_DIGITS` | 15 | significant digits of `viper_scpi_result_double()` (1..17) |
| `VIPER_SCPI_DEFAULT_MANUFACTURER` etc. | `VIPER`, `SCPI`, `0`, `0.1.0` | `*IDN?` fields when the configuration leaves them NULL |

## Memory

All state is in the context you allocate; the library has no writable static
data (CI fails the build if it ever does).

| | Size with default limits |
| --- | --- |
| `viper_scpi_context_t` on Cortex-M (32-bit) | 444 bytes |
| `viper_scpi_context_t` on a 64-bit host | 664 bytes |
| output buffer (message-exchange model) | `output_size`, your choice |
| stack | about 0.6 KB for a typical three-level command, up to about 2 KB for a header of 8 mnemonics through optional nodes that finally fails to match, plus your callback's own use (see below) |

Stack figures were measured on an emulated Cortex-M0+ (arm-none-eabi-gcc
13.2, `-Os`), from `viper_scpi_input_buffer()` down: `SYST:ERR?` needs about
630 bytes; a header of 8 mnemonics through nodes such as `B[:Q2]` that fails
at the last one needs about 1960, because tree resolution recurses with 56
bytes for every mnemonic matched or skipped, 56 more at the end of every
node's pattern, and 32 per tree level. The C library's `strtod`/`snprintf` add their own stack when
doubles are used.

The largest contributors are the line buffer (`VIPER_SCPI_BUFFER_SIZE`), the
parameter and path token arrays (two pointers each per entry) and the error
queue (a code and a detail pointer per entry).

## Code size

Measured with arm-none-eabi-gcc 13.2, `-Os`, Cortex-M0+, newlib, `--gc-sections`
(`.github/linktest.sh`):

| Program | Text |
| --- | --- |
| all library objects together | 11.7 KB |
| a program using integer parameters and responses, `*IDN?`, `*STB?`, `SYST:ERR?` | 9.4 KB in total, startup code included |
| the same program also using one double parser and `viper_scpi_result_double()` | 53 KB |

The difference is the C library's `snprintf`/`strtod` and the soft-float code
behind them. All floating-point code of viper-scpi lives in
`src/scpi_float.c`, the only file that calls them, so firmware that never calls
a `viper_scpi_param_double*` or `viper_scpi_result_double*` function does not
link it -- provided the library is linked as a static archive or with
unused-section removal (GNU `--gc-sections`, TI's default). If you need doubles,
newlib-nano with float printf support or a vendor C library is usually much
smaller than full newlib.

## Calling rules

- Everything for one context from one execution context (main loop or one RTOS
  task). Interrupt handlers queue bytes and set flags.
- Callbacks must not call the `viper_scpi_input_*` functions or
  `viper_scpi_end_of_message()`.
- Strings handed to the library (`idn_*`, error texts, error detail, unit and
  choice tables) are not copied and must stay valid.
