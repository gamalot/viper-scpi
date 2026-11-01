# Changelog

All notable changes to viper-scpi. Versions follow [Semantic Versioning](https://semver.org/);
while the major version is 0, a minor release may contain breaking changes,
and those are marked **Breaking**. Each release is tagged `vX.Y.Z`.

## [Unreleased]

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
- Documentation guides in `docs/`.

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

[0.2.0]: https://github.com/gamalot/viper-scpi/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/gamalot/viper-scpi/releases/tag/v0.1.0
