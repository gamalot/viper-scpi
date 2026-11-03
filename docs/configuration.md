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
| `VIPER_SCPI_MAX_RECURSION_DEPTH` | 8 | mnemonics per header, including the inherited path (3..255: the built-in `SYSTem:ERRor:NEXT?` and `STATus:OPERation:ENABle` have 3) |
| `VIPER_SCPI_MAX_SUFFIXES` | 4 | numeric suffixes recorded per command, 1..255 (later ones read as the default) |
| `VIPER_SCPI_ERROR_QUEUE_SIZE` | 8 | error queue entries (2..255) |
| `VIPER_SCPI_DOUBLE_DIGITS` | 0 | significant digits of `viper_scpi_result_double()`: 1..17, or 0 for `DBL_DIG` of the platform (15 where double is binary64, 6 where it is binary32 as with avr-gcc) |
| `VIPER_SCPI_DEFAULT_MANUFACTURER` etc. | `VIPER`, `SCPI`, `0`, `0.1.0` | `*IDN?` fields when the configuration leaves them NULL |

## Memory

All state is in the context you allocate; the library has no writable static
data (CI fails the build if it ever does).

| | Size with default limits |
| --- | --- |
| `viper_scpi_context_t` on Cortex-M (32-bit) | 356 bytes |
| `viper_scpi_context_t` on a 64-bit host | 424 bytes |
| output buffer (message-exchange model) | `output_size`, your choice |
| stack | about 0.55 KB for a typical command, up to about 1.2 KB for a header of 8 mnemonics that fails at the last one, plus your callback's own use (see below) |

Stack figures were measured on an emulated Cortex-M0+ (arm-none-eabi-gcc
13.2, `-Os`), from `viper_scpi_input_buffer()` down: `SYST:ERR?` needs about
550 bytes; a header of 8 mnemonics through nodes such as `B[:Q2]` that fails
at the last one about 1190. Tree resolution costs 88 bytes per tree level
(56 + 32, `-fstack-usage`) plus 56 for each optional mnemonic that is
consumed (it may have to be skipped instead); required mnemonics are matched
without recursion. The C library's `strtod`/`snprintf` add their own stack when
doubles are used.

The largest contributors are the line buffer (`VIPER_SCPI_BUFFER_SIZE`), the
parameter and path token arrays (4 bytes per entry: a 16-bit offset into the
line buffer and a length, which is why the buffer is limited to 65535 bytes)
and the error queue (a code and a detail pointer per entry).

## Code size

Measured with arm-none-eabi-gcc 13.2, `-Os`, Cortex-M0+, newlib, `--gc-sections`
(`.github/linktest.sh`):

| Program | Text |
| --- | --- |
| all library objects together | 13.1 KB |
| a program using `viper_scpi_param_integer()` without units, integer responses, `*IDN?`, `*STB?`, `SYST:ERR?` | 10.4 KB in total, startup code included |
| the same program also taking `VOLT 1500 MV` into millivolts with a unit table | 11.1 KB |
| the same program also using one double parser and `viper_scpi_result_double()` | 53 KB |

Which files a program links depends on what it names:

- `src/scpi_float.c` (and with it the C library's `snprintf`/`strtod` and the
  soft-float code behind them) only if it calls `viper_scpi_param_double()`,
  `viper_scpi_param_number()` or a `viper_scpi_result_double*()` function.
  That is the 42 KB difference above.
- `src/scpi_units.c` (the unit parser, multiplier prefixes and the
  ready-made tables, about 1.1 KB) only if it uses a unit table: one of
  `viper_scpi_units_*` or its own `VIPER_SCPI_UNIT_TABLE()`. The parsers
  reach the unit parser through the table, so `viper_scpi_param_integer()`
  or `_number()` with `units` NULL leaves it out.

Both hold provided the library is linked as a static archive or with
unused-section removal (GNU `--gc-sections`, TI's default); the CI link test
checks them. If you need doubles,
newlib-nano with float printf support or a vendor C library is usually much
smaller than full newlib. The C library must format `%G`: without that
(newlib-nano without `-u _printf_float`, avr-libc's default `vfprintf`) a
double response reads `9.91E+37` and queues -200 "printf has no
floating-point support". `-ffast-math` is fine: INF and NAN are recognised
from their bit patterns.

## Calling rules

- Everything for one context from one execution context (main loop or one RTOS
  task). Interrupt handlers queue bytes and set flags.
- Callbacks must not call the `viper_scpi_input_*` functions or
  `viper_scpi_end_of_message()`.
- Strings handed to the library (`idn_*`, error texts, error detail, unit and
  choice tables) are not copied and must stay valid.
