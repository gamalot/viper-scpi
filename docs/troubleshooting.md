# Troubleshooting

[Documentation home](README.md)

Start with `SYSTem:ERRor?` -- read it until it answers `0,"No error"`. The
code usually points straight at the cause.

## By error code

**-113 Undefined header**
- A typo, or the short form is wrong: `VOLTage` accepts only `VOLT` and
  `VOLTAGE`, never `VOLTA`.
- A relative path: `SOUR:VOLT 1;SOUR:CURR 2` means `SOUR:SOUR:CURR` (SCPI-99).
  Write `SOUR:VOLT 1;CURR 2` or `SOUR:VOLT 1;:SOUR:CURR 2`, or enable
  `VIPER_SCPI_OPT_RELATIVE_FALLBACK` if you cannot change the host.
- The query form of a set-only command (or the reverse): the callback returns
  -113 for the form it does not support, and `*RST?`/`*IDN` are -113.
- `*TRG` without a `trigger` hook in the configuration.
- A malformed node pattern (unclosed `[`) never matches. Test every command.

**-363 Input buffer overrun**
- The program message is longer than `VIPER_SCPI_BUFFER_SIZE` (128 by
  default). Raise it, split the message, or use [streaming block input](large-data.md)
  for large blocks.
- Bytes arrived while the parser was busy with `*WAI`/`*OPC?`: the transport
  must hold input off while `viper_scpi_busy()` is true.
- In the stream model, a missing `\n` makes consecutive commands one long line.

**-410 Query INTERRUPTED** (message-exchange model)
- The controller sent a new message before reading the previous reply.
  Usually a host that writes `*IDN?` and then immediately writes again; or one
  transfer containing `*IDN?\n*OPC?\n`, which is two messages by the standard
  (`VIPER_SCPI_OPT_EOM_MESSAGES` makes it one).
- A read timed out on the host, which then sent the next command.

**-420 Query UNTERMINATED** (message-exchange model)
- The controller asked for a reply that does not exist: it read after a
  command that is not a query, or read twice. Your transport pushes this code.
- It appears after `INIT;*OPC?`: the transport pushed it while the reply was
  still being produced. Push it only if `viper_scpi_busy()` is false, and
  otherwise answer the read once the reply is complete (see
  [Transports](transports.md#message-exchange-model)).

**-321 Out of memory**
- A reply is larger than `output_size`. Increase it, or produce large replies
  with `viper_scpi_result_block_fill()`.

**-161 Invalid block data**
- The message ended (EOM, or LF on a stream transport) before a block had all
  its declared bytes -- or right after the `#` or inside its length digits.
  Check the length digits the host computed; `#3100` announces 100 bytes.
- A definite block that is not complete where its parameter ends (`#1;`,
  `#25x`).
- A parameter that is a bare `#` (`LED #`, `X #,1`): the host meant to send a
  block or a `#H` number and left out the rest.

**-104 Data type error**
- A number where text was expected or the reverse; a unit given to a plain
  `viper_scpi_param_double()` (use the `_unit` parsers); an unquoted string.

**-222 Data out of range**
- The value is outside the range the callback passes to a `_kw` parser, or
  outside the integer type. `INF` fails any finite range.

**-350 Queue overflow**
- More than `VIPER_SCPI_ERROR_QUEUE_SIZE` errors without anyone reading them.
  The host should poll `SYST:ERR?` (or check EAV in the status byte) after
  each command group.

## By symptom

**The first `*ESR?` after power-on returns 128.** That is PON, required by
IEEE 488.2. Send `*CLS` or read `*ESR?` once at startup in your test scripts.

**Numbers come back with more digits than were written** (`0.1` reads back as
`0.1`, but a computed value shows 15 digits). Responses use 15 significant
digits. Use `viper_scpi_result_double_digits()` to report the resolution the
instrument really has.

**No service request is ever raised.** Check that the host enabled it
(`*SRE`, and `*ESE` or `STAT:...:ENAB` for the bits feeding it), that the main
loop calls `viper_scpi_take_service_request()` and that the transport sends
something when it returns true.

**VISA `viReadSTB()` keeps showing bit 6 after an SRQ.** On USB488 the
interrupt-IN notification is the serial poll: produce its status byte with
`viper_scpi_serial_poll()` (or call `viper_scpi_rqs_delivered()`), so RQS is
cleared when the notification goes out.

**`*OPC?` never answers.** An operation was begun with `viper_scpi_op_begin()`
and never ended, or the main loop does not call `viper_scpi_poll()`.

**The reply is empty although the command ran.** In the message-exchange model
the reply stays hidden while the message is suspended by `*WAI`/`*OPC?`;
`viper_scpi_output_pending()` is false until `viper_scpi_poll()` has finished
the message.

**Linker pulls in printf even though no doubles are used.** The library must be
linked as a static archive (or with `--gc-sections`), and no code may reference
a `viper_scpi_param_double*` or `viper_scpi_result_double*` function -- including
the unit and `_kw` double parsers.

**Commands behave differently in a terminal and in a VISA program.** The two
output models differ: in the message-exchange model (`output_buffer` set) EOM
also ends a message, LF inside a quoted string is string data, replies wait to
be read and `-410`/`-420` apply; in the stream model LF always ends a message
and replies are written at once. Check which model each configuration selects.
