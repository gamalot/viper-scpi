# Status and errors

[Documentation home](README.md)

## The IEEE 488.2 status model

```
 Error queue ──────────────► EAV (bit 2) ─┐
 STATus:QUEStionable summary ► QUES (bit 3) ┤
 Reply waiting ─────────────► MAV (bit 4) ─┤      Status Byte (*STB?)
 ESR & ESE ≠ 0 ─────────────► ESB (bit 5) ─┼──► & SRE ≠ 0 ──► MSS/RQS (bit 6)
 STATus:OPERation summary ──► OPER (bit 7) ┘
```

| Register | Read | Write | Cleared by |
| --- | --- | --- | --- |
| Status byte | `*STB?` (MSS in bit 6), serial poll (RQS in bit 6) | -- | the conditions behind its bits |
| Service request enable | `*SRE?` | `*SRE <0..255>` (bit 6 ignored) | -- |
| Event status register (ESR) | `*ESR?` (clears it) | `viper_scpi_event_set()` | `*ESR?`, `*CLS` |
| Event status enable | `*ESE?` | `*ESE <0..255>` | -- |

ESR bits: OPC 0 (`*OPC`), QYE 2 (-400..-499), DDE 3 (-300..-399 and positive
device codes), EXE 4 (-200..-299), CME 5 (-100..-199), URQ 6 (yours, via
`viper_scpi_event_set()`), PON 7 (set by `viper_scpi_init()`).

Pushing an error sets its ESR bit automatically. A controller that wants a
service request on any error sends `*ESE 60;*SRE 32` (CME|EXE|DDE|QYE into ESB,
ESB into MSS).

`PON` is set at initialisation, so the first `*ESR?` after power-on reads 128
(or more). That is required by IEEE 488.2 and tells the host that the device
restarted.

## Service requests

The library tracks every rising edge of `STB & SRE` as it happens -- after each
command, each error and each change of the output queue -- and
`viper_scpi_take_service_request()` reports each one once. See
[Transports](transports.md#srq-and-serial-poll) for raising SRQ and for serial
polls.

## The SCPI STATus subsystem

Two register sets, OPERation and QUEStionable, each with condition, event,
enable and two transition filters. 16 bits each; bit 15 is always 0.

```
 condition ──► transition filter ──► event (latched) ──► & enable ──► summary bit in STB
             PTR: 0→1, NTR: 1→0
```

Commands (built in; a user tree may override them):

| Command | Effect |
| --- | --- |
| `STATus:OPERation[:EVENt]?` | event register, then clears it |
| `STATus:OPERation:CONDition?` | condition register |
| `STATus:OPERation:ENABle <n>` / `?` | enable mask, 0..32767 |
| `STATus:OPERation:PTRansition <n>` / `?` | 0→1 filter |
| `STATus:OPERation:NTRansition <n>` / `?` | 1→0 filter |
| the same under `STATus:QUEStionable` | |
| `STATus:PRESet` | enables 0, PTR all ones, NTR 0 (also the power-on state) |

`*CLS` clears both event registers; `STAT:PRES` leaves events and conditions
alone.

The instrument reports its state by setting condition bits; transitions,
latching, summaries and service requests follow:

```c
/* SCPI-99 bit assignments: OPERation 4 = MEASuring, QUEStionable 1 = CURRent */
#define OPER_MEAS (1u << 4)
#define QUES_CURR (1u << 1)

viper_scpi_status_condition(ctx, VIPER_SCPI_STATUS_OPER, OPER_MEAS, measuring ? OPER_MEAS : 0);
viper_scpi_status_condition(ctx, VIPER_SCPI_STATUS_QUES, QUES_CURR, overcurrent ? QUES_CURR : 0);
```

A host that wants a service request when a measurement finishes:
`STAT:OPER:PTR 0;NTR 16;ENAB 16;*SRE 128` (event on MEASuring 1→0, OPER summary
into MSS).

## The error queue

Every failed command leaves an entry; the controller reads them oldest first
with `SYSTem:ERRor?` (`<code>,"<text>"`; `0,"No error"` when empty).
`SYSTem:ERRor:COUNt?` tells how many are queued. The queue holds
`VIPER_SCPI_ERROR_QUEUE_SIZE` (8) entries; when it is full the newest entry
becomes `-350,"Queue overflow"`, as the standard requires. `*CLS` empties it.

From C:

| Function | Use |
| --- | --- |
| return a code from a callback | the normal way |
| `viper_scpi_error_push(ctx, code)` | more than one error, or outside a command |
| `viper_scpi_error_push_info(ctx, code, "detail")` | with text appended to the message |
| `viper_scpi_error_pop()` / `_pop_info()` | read entries from C (e.g. a front-panel display) |
| `viper_scpi_error_count()`, `_clear()` | |

### Your own error codes and texts

SCPI reserves positive codes for the device. Give them texts in the
configuration; the table is searched before the built-in texts, so it can also
reword a standard code:

```c
static const viper_scpi_error_text_t my_errors[] = {
    { 101, "Relay stuck" },
    { 102, "Overtemperature" },
    { 0, NULL }
};
/* cfg: .error_texts = my_errors */
```

### Detail text

```c
if (v > 30.0) {
    viper_scpi_error_push_info(ctx, VIPER_SCPI_RES_ERR_OUT_OF_RANGE, "VOLT max is 30 V");
    return VIPER_SCPI_RES_OK;   /* already queued */
}
```

`SYSTem:ERRor?` then answers `-222,"Data out of range;VOLT max is 30 V"`. The
detail string is **not copied**: pass a string literal or other static storage,
never a stack buffer.

## Built-in error codes

| Code | Text | Raised by the library when |
| --- | --- | --- |
| -100 | Command error | (for your use) |
| -102 | Syntax error | malformed header or parameter list, unterminated string |
| -104 | Data type error | a parameter of the wrong kind |
| -108 | Parameter not allowed | too many parameters; a parameter after a streamed block |
| -109 | Missing parameter | a required parameter is absent |
| -112 | Program mnemonic too long | more than 8 mnemonics in a header |
| -113 | Undefined header | no such command, or the wrong form (query/command) |
| -114 | Header suffix out of range | (for your callbacks) |
| -131 | Invalid suffix | unknown or disallowed unit |
| -160 / -161 | Block data error / Invalid block data | a message ended inside a definite-length block |
| -200 | Execution error | (for your use) |
| -220 | Parameter error | invalid arguments to a parser (a programming error) |
| -222 | Data out of range | a number outside its range |
| -224 | Illegal parameter value | an unknown choice; NAN in a range |
| -321 | Out of memory | a reply larger than `output_size` |
| -350 | Queue overflow | the error queue was full |
| -363 | Input buffer overrun | a message larger than `VIPER_SCPI_BUFFER_SIZE`; input while busy |
| -400 | Query error | a second generated block in one reply |
| -410 | Query INTERRUPTED | a new message over an unread reply |
| -420 | Query UNTERMINATED | (pushed by the transport) a read with nothing queued |

What these usually mean when they show up unexpectedly: see
[Troubleshooting](troubleshooting.md).
