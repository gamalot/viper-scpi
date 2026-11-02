# Changelog

All notable changes to viper-scpi. Versions follow [Semantic Versioning](https://semver.org/);
while the major version is 0, a minor release may contain breaking changes,
and those are marked **Breaking**. Each release is tagged `vX.Y.Z`.

## [Unreleased]

### Fixed

- Input framing is decided in one place for every byte, also for bytes that
  are not kept. As a result:
  - A message refused while another is suspended (`*WAI`, `*OPC?`) is dropped
    whole and reported once as -363, even if the suspension ends before its
    last byte arrives. Before, its tail ran as a separate command (`SOUR2:`
    refused, then `VOLT 30` set channel 1), and a LF inside a refused block
    let block data run as commands.
  - After a streamed block, the rest of a command with a surplus parameter is
    skipped with strings and blocks recognised; a `;` inside them no longer
    starts a command (`MMEM:UPL #15hello,'a;*RST'` ran `*RST`).
  - A message that ends inside a block's length digits (`RAW #1<LF>`,
    `RAW #1<CR><LF>`) is dropped with -161 like one that ends right after the
    `#`; an incomplete definite block inside a message (`#1;`, `#25x`) is
    -161 instead of being passed on as text.
  - In the message-exchange model a LF inside an indefinite `#0` block is
    block data; the block ends at EOM (IEEE 488.2 NL^END). Before, binary
    data containing 0x0A was cut and its tail executed.
  - The input state machine and the line executor agree on where a block
    starts after a block's last byte.
- CR, NUL, VT, FF and the other control bytes are IEEE 488.2 white space
  instead of being deleted: `VOLT 1<CR>2` is -104, not 12, and `VOLT<CR>5`
  sets 5. `\r\n` still ends a line.
- A response message is ended in one place. A streamed first command's reply
  is terminated (and dropped whole after -321) when the rest of the message is
  dropped with -161 or -363 or by `viper_scpi_input_discard()`; before, it was
  handed out without its `\n` and glued to the next reply.
- The partial reply of a running or suspended message no longer raises MAV for
  service requests, serial polls and `viper_scpi_output_read()` (*STB? inside
  the message still sees it). A suspended `*IDN?;INIT;*WAI` with `*SRE 16`
  raised an SRQ for a reply that could not be read.
- `viper_scpi_output_discard()` during a suspended message no longer makes
  the resumed reply start with `;`.
- A relative header after a streamed command inherits its path (SCPI-99
  6.2.4) instead of being resolved from the root, which could run a root node
  of the same name.
- A line of only white space is not a message and no longer interrupts a
  reply with -410.
- `viper_scpi_error_pop()`, `_pop_info()` and `_clear()` update the
  service-request state, so a later request from another source is not lost;
  a request whose reason went away before it was taken is no longer reported.
- Queue overflow sets DDE in the ESR for the -350 it queues.
- Commas inside parentheses (expression data, SCPI channel lists such as
  `(@1,2,3)`) no longer split parameters.
- A streamed block longer than `SIZE_MAX` (16-bit `size_t`) is rejected with
  -222 instead of reporting a truncated length at BEGIN.
- Integer parameters take decimal numeric data and round it, half away from
  zero, in integer arithmetic (IEEE 488.2 7.7.2): `*ESE 32.0`, `*SRE 3.2E1`
  and `STAT:OPER:ENAB 16.0` were -104. Booleans take a number too (non-zero
  is ON, SCPI-99 7.3).
- `*ESE` and `*SRE` reject a second parameter with -108, like the STATus
  registers; it was silently ignored.
- `viper_scpi_param_string()` leaves its destination unchanged when it
  rejects a string (-104, -222); it used to overwrite it, and leave it
  without a NUL on -222.
- Unit prefixes scale by an exact power of ten, so `10US` is the same double
  as `10E-6` and a value on a range limit given in base units is accepted
  (`10US` with a minimum of 10e-6 was -222).
- `viper_scpi_param_int32_kw()` reports `NAN` as -224 and `INF`/`NINF` as
  -222, as documented and as the double parsers do (was -104).
- A path with more `#` mnemonics than `VIPER_SCPI_MAX_SUFFIXES` matches; the
  extra suffixes read as the default (it was -113).
- Doubles are parsed and formatted with '.' as the radix also when a hosted
  application has set a locale with a decimal comma (`VOLT 1.5` set 1, and
  replies read `2,25`).
- The overflow checks use `DBL_MAX` (a binary64 literal was out of range and
  never fired with a 32-bit `double`).
- `viper_scpi_result_block_fill()` writes the full length into the block
  header on 16-bit `size_t` targets (it wrote the length modulo 65536 and
  then produced every byte, so the host lost framing).
- A `write` hook that returns less than it was given ends that response
  message: the rest, terminator included, is not written and -321 is queued.
  The short count was ignored, so a host read a cut-off value (`12` for
  `12.5`) as a complete one.
- An error code outside -32768..32767 is queued as -200 instead of being
  wrapped into another code (40000 read back as -25536).
- The compile-time limits are checked: a value outside the documented range
  (for example `VIPER_SCPI_ERROR_QUEUE_SIZE` 256, which wrapped the queue
  count) is a compile error.

### Added

- `viper_scpi_expect(ctx, set_params, query_params)`: the usual first line of
  a callback; -113 for a form the command does not have
  (`VIPER_SCPI_NO_FORM`), -108 for surplus parameters. The built-in commands
  use it, so all of them now check the form before the parameters
  (`SYST:VERS 5` is -113 like `*IDN 5`, not -108).
- `cfg.common`: the instrument's own common commands (`*SAV`, `*RCL`,
  `*OPT?`, ...), searched before the built-in ones, which they may replace;
  `cfg.clear` is called by `*CLS`, and `cfg.self_test` gives `*TST?` its
  result (it always answered 0).
- `viper_scpi_defer()` and `viper_scpi_resumed()`: a slow query suspends its
  message and is called again by `viper_scpi_poll()` to write its result once
  no operation is pending, instead of blocking the main loop.
- `viper_scpi_fail(ctx, code, detail)` queues an error with detail and
  returns `VIPER_SCPI_RES_QUEUED`, so callbacks and stream handlers fail in
  one statement without queuing the error twice, also with device codes.
- `viper_scpi_init()` is a macro that passes `sizeof(viper_scpi_context_t)`
  to the new `viper_scpi_init_sized()`, which refuses (-220, context
  untouched) a context whose size differs from the library's: the library
  and the application were built with different limits. `(viper_scpi_init)`
  remains a function.
- `viper_scpi_input_until_busy()`: feeds a chunk up to the message that
  suspends execution and returns how many bytes it consumed, so a transport
  can keep pipelined commands instead of having them refused.

### Changed

- The context grew by 8 bytes on Cortex-M (444).
- Header resolution rejects a sibling whose required mnemonic starts with a
  different letter before parsing its pattern, and the relative-path
  fallback no longer copies the path (which linked `memmove` into every
  image).
- The documented stack figures were wrong: the worst case is about 2 KB, not
  1.4 KB (docs/configuration.md).

## [0.2.1] - 2026-11-01

### Fixed
- A message that ends right after a block's `#` (EOM, or LF on a stream
  transport) is dropped with `-161`, like one that ends later in the block;
  it used to run with `#` as the parameter (e.g. `-224` from a choice
  parser, or acted upon by a raw/string parameter). A `#` followed by `;`,
  `,` or whitespace now fails that command with `-161` as well. `#H`, `#Q`,
  `#B`, other `#`+letter text, `#0` and complete blocks are unchanged.

### Added
- `VIPER_SCPI_RES_ERR_SUFFIX_RANGE` (-114, "Header suffix out of range") for
  callbacks that reject a numeric suffix.
- Documentation guides in `docs/`, and `tools/docs_to_wiki.py` to publish them
  as the GitHub wiki.

## [0.2.0] - 2026-11-01

### Added
- Message-exchange output model for USBTMC, VXI-11, HiSLIP and GPIB device
  transports: queued replies read with `viper_scpi_output_read()`, MAV in the
  status byte, `-410` Query INTERRUPTED, `-321` for replies that do not fit,
  generated block responses (`viper_scpi_result_block_fill()`),
  `viper_scpi_device_clear()`, service-request edges
  (`viper_scpi_take_service_request()`) and `*TRG` through a trigger hook.
- `viper_scpi_input_discard()`: drop a partial message and keep the reply.
- `viper_scpi_serial_poll()` and `viper_scpi_rqs_delivered()`: RQS latched in
  the context for GPIB/USB488/VXI-11 serial polls.
- Overlapped commands: `viper_scpi_op_begin()/op_end()`, deferred `*OPC`,
  `*OPC?` and `*WAI` that suspend without blocking and resume from
  `viper_scpi_poll()`, `viper_scpi_busy()` for flow control.
- SCPI STATus subsystem (OPERation, QUEStionable, `STATus:PRESet`) with
  `viper_scpi_status_condition()`; summaries in status byte bits 7 and 3.
- Streaming block input for blocks larger than the line buffer
  (`viper_scpi_config_t.stream_nodes`).
- Device-specific error texts (`viper_scpi_config_t.error_texts`) and error
  detail (`viper_scpi_error_push_info()`, `viper_scpi_error_pop_info()`,
  `viper_scpi_error_text()`).
- `INFinity`, `NINFinity` and `NAN` in the double parsers; `UP`/`DOWN` through
  `viper_scpi_param_double_kw_ex()`, `_int32_kw_ex()`, `_double_unit_kw_ex()`.
- Opt-in leniencies in `viper_scpi_config_t.options`:
  `VIPER_SCPI_OPT_RELATIVE_FALLBACK`, `VIPER_SCPI_OPT_EOM_MESSAGES`.
- `viper_scpi_result_double_digits()` and `VIPER_SCPI_DOUBLE_DIGITS`.
- Version macros `VIPER_SCPI_VERSION_MAJOR/MINOR/PATCH` and
  `VIPER_SCPI_VERSION_STRING`; this changelog.
- Error codes `-131`, `-160`, `-161`, `-321`, `-400`, `-410`, `-420`.

### Changed
- **Breaking:** `viper_scpi_init()` sets PON in the ESR (IEEE 488.2
  11.5.1.1.2), so the first `*ESR?` after power-on reads 128.
- **Breaking:** double responses use an upper-case exponent and 15
  significant digits instead of 9 (`1E-06`, not `1e-06`).
- **Breaking:** a message that ends inside a definite-length block is dropped
  with `-161` instead of running the command (which saw `-104`).
- **Breaking:** `INF`, `NINF` and `NAN` are accepted by the double parsers
  where they were `-104`.
- **Breaking (message-exchange model):** LF and CR inside a quoted string are
  string data instead of message terminators.
- **Breaking:** the status byte reports the OPER (bit 7) and QUES (bit 3)
  summaries, and `*CLS` clears the STATus event registers.
- Integer responses are formatted without `snprintf`; all floating-point code
  is in `src/scpi_float.c`, so integer-only firmware links no printf/strtod.
- Integer parsing uses 32-bit arithmetic only (no 64-bit helpers on Cortex-M0).
- Keyword matching no longer calls `strlen()` per attempt.
- Configuration structs must be initialised with designated initializers:
  new fields are appended, and zero keeps the previous behaviour.

The context grew from 472 to 648 bytes (64-bit host, default limits).

## [0.1.0] - 2026-09-30

First version: tree-structured command dispatch with long/short forms,
`A[:B]:C` patterns and numeric suffixes, SCPI relative paths, parameter
parsers (integers in several radixes, doubles, units, MIN/MAX/DEF, booleans,
choices, strings, block data), IEEE 488.2 mandatory commands, the SCPI error
queue with `SYSTem:ERRor?`, and the stream output model.

[Unreleased]: https://github.com/gamalot/viper-scpi/compare/v0.2.1...HEAD
[0.2.1]: https://github.com/gamalot/viper-scpi/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/gamalot/viper-scpi/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/gamalot/viper-scpi/releases/tag/v0.1.0
