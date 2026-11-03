# viper-scpi — improvement backlog

Work items for whoever picks up viper-scpi next. Each item says what is wrong
or missing, how that was established, what to change, and how to know it is
done. Items are ordered by priority within each section; take them roughly in
that order, one commit per item.

State of the library when this was written: `main` at `2a573f5` (message-exchange
output model added), all CI jobs green (run 41305521159).

> **Status (2026-11-01):** every item below has been implemented on `main`, one
> commit per item (commit subjects end in the item number, e.g. "(2.3)"), and
> is recorded in [CHANGELOG.md](../CHANGELOG.md) as release 0.2.0, tagged
> `v0.2.0` (`v0.1.0` marks `d385786`). Item 1.5 cites IEEE 488.2 7.7.5 without the standard text having been
> checked first-hand.
>
> **Open (2026-11-03):** section 6 lists what was found after 0.4.0 (section 5,
> found after 0.2.0, is fixed).

## Ground rules

These hold for every item. A change that breaks one is not done.

- **ISO C99, zero allocation, no blocking.** No `malloc`/`free`, no busy waits,
  no RTOS calls. CI checks the first.
- **No writable static data in the library.** The `cortex-m` CI job fails on any
  `.data`/`.bss`. State goes in `viper_scpi_context_t`.
- **Context stays under 2 KB** with default limits (the `strict-c99` job checks
  it; it is 520 bytes today). Say in the commit message how much an item adds.
- **Warning-free** under every CI configuration, including
  `-std=c99 -pedantic-errors -Wshadow -Wconversion`, ASan/UBSan and clang-tidy.
- **Every behaviour change has a test** in `tests/`, and every new public
  function is documented in `viper_scpi.h` and the README.
- **Configuration structs are initialised with designated initializers**
  everywhere (README, examples, tests). New `viper_scpi_config_t` fields go at
  the end, and a zero/NULL value must mean "previous behaviour".
- **Do not weaken standard behaviour to make a test pass.** Several items below
  exist precisely because an earlier parser was lenient where IEEE 488.2 / SCPI-99
  is not; where an item offers leniency it is opt-in.
- **Breaking changes are named as such** in the commit message and in
  `CHANGELOG.md` once item 4.1 exists. (Example of one that already happened:
  `viper_scpi_init()` now sets PON in the ESR.)

How to run everything CI runs, locally on Linux:

```sh
cmake -S . -B build -DVIPER_SCPI_WERROR=ON && cmake --build build && ctest --test-dir build --output-on-failure
cmake -S . -B build-asan -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug -DVIPER_SCPI_WERROR=ON \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all" && \
      cmake --build build-asan && ctest --test-dir build-asan --output-on-failure
# plus the strict-c99, clang-tidy and cortex-m commands in .github/workflows/ci.yml
```

### Who uses it

The first production user is a USBTMC/USB488 instrument on an LP-MSPM0G5187
(Cortex-M0+, TI Arm Clang 5.1.1, TinyUSB). It uses the message-exchange model
(`output_buffer`, `viper_scpi_output_read`, `viper_scpi_take_service_request`,
`viper_scpi_device_clear`, and since 0.2.0 `viper_scpi_input_discard`,
`viper_scpi_serial_poll`, `viper_scpi_rqs_delivered`), parses integers only
(since 0.4.0 with `viper_scpi_param_integer`, never doubles) and is verified
against a real VISA with a 57-item hardware test. That project is not in this repository, so it cannot be run
from here; keep the message-exchange semantics in `tests/test_message.c`
intact and extend that suite rather than rely on hardware.

---

## 1. Correctness and standard conformance

### 1.1 NR3 responses use a lower-case exponent and only 9 significant digits

**Problem.** `viper_scpi_result_double()` formats with `"%.9g"`. Measured
output (host build, `main` at `2a573f5`):

| value | output |
| --- | --- |
| `1e10` | `1e+10` |
| `1.5e-7` | `1.5e-07` |
| `123456789012` | `1.23456789e+11` (last three digits lost) |

IEEE 488.2 §8.7.4 (NR3 response data) writes the exponent with an upper-case
`E`. Nine digits also cannot round-trip a `double`, so a value written with
`VOLT 1.23456789012` reads back different from what was set.

**Change.**
- Always emit `E` (post-process the `snprintf` output, or see item 3.1).
- Make the precision a compile-time default (`VIPER_SCPI_DOUBLE_DIGITS`,
  default 15 or 17 — justify the choice) and add
  `viper_scpi_result_double_digits(ctx, value, digits)` for instruments that
  want to report only meaningful digits (e.g. a 6½-digit DMM).
- Keep the existing 9.9E37 / 9.91E37 mapping for ±INF / NaN.

**Done when** tests cover the three values above, negative values, zero,
values that `%g` prints without an exponent, and the INF/NaN cases, and
existing tests that compare double output are updated deliberately.

### 1.2 An incomplete block at the end of a message reaches the command

**Problem.** If a message ends (`viper_scpi_end_of_message()`, or a `\n` for
an indefinite block) while a definite-length block is still short of its
declared length, the line is executed anyway. Measured: `BLK #15ab` + EOM runs
the `BLK` callback, whose `viper_scpi_param_block()` returns `-104`. The command
should never run on a block the controller did not finish sending.

**Change.** In `finish_line()`, if `block_state` is `BLK_DATA` (or `BLK_LENGTH`),
drop the line and push **-161 "Invalid block data"** (SCPI-99 error list) instead of
executing it. Add `-160`/`-161` to the result enum and the error text table.

**Done when** tests show: truncated definite block + EOM → `-161`, callback not
called, next message unaffected; a complete block is unaffected; the fuzz test
still passes under sanitizers.

### 1.3 No way to drop partial input without dropping the queued reply

**Problem.** Transports need to discard a half-received message on their own
(USBTMC `INITIATE_ABORT_BULK_OUT`, `CLEAR_FEATURE` on bulk OUT, a VXI-11 link
abort) without touching a reply the controller may still read. The only API is
`viper_scpi_device_clear()`, which also drops the reply. The MSPM0 firmware
uses it for those cases today and says so in a comment.

**Change.** Add `void viper_scpi_input_discard(viper_scpi_context_t *ctx)`:
resets line assembly (`length`, `overflow`, block and quote state) and nothing
else. Have `viper_scpi_device_clear()` call it.

**Done when** a test shows a queued reply surviving `viper_scpi_input_discard()`
while the half message is gone, and README's transport table lists it.

### 1.4 Serial-poll (RQS) semantics are left to every transport

**Problem.** IEEE 488.2 distinguishes MSS (bit 6 as `*STB?` reports it) from RQS
(bit 6 as a serial poll reports it, latched when a service request is raised and
cleared by the poll). The library only provides MSS, so each transport keeps its
own RQS latch. The MSPM0 USBTMC firmware does (`s_rqs` in its `usbtmc_app.c`),
including one non-obvious rule learned on hardware: when the transport's SRQ
notification itself carries the status byte (USB488 interrupt-IN), the host's
VISA hands that byte to the next `viReadSTB()` without asking the device, so
delivering the notification *is* the serial poll and must clear RQS.

**Change.**
- Latch RQS inside the context when `viper_scpi_take_service_request()` reports
  an edge.
- `uint8_t viper_scpi_serial_poll(viper_scpi_context_t *ctx)`: status byte with
  RQS in bit 6, then clears RQS (GPIB serial poll, USB488 READ_STATUS_BYTE,
  VXI-11 device_readstb).
- `void viper_scpi_rqs_delivered(viper_scpi_context_t *ctx)`: clears RQS, for
  transports whose SRQ message carries the status byte.

**Done when** tests cover: no request → bit 6 clear; request → first poll has
bit 6, second does not; `*STB?` still reports MSS and never clears anything;
`rqs_delivered` clears it; a new edge sets it again.

### 1.5 Opt-in handling of a newline inside quoted string data

**Problem.** `STR "a<LF>b"<LF>` is split at the first LF and produces two `-102`.
On stream transports LF is the only message terminator, so this is defensible
there. On message-exchange transports the message end is signalled out of band
(USBTMC EOM, VXI-11 END), and IEEE 488.2 §7.7.5 string data may contain any
7-bit ASCII character.

**Change.** Check the standard text first. If confirmed, in the message-exchange
model only (`output_buffer != NULL`), treat LF inside an open quote as data.
Leave the stream model as it is.

**Done when** the decision and the standard reference are in the README, with
tests for both models.

---

## 2. Features real instruments need

### 2.1 Overlapped commands: `*OPC`, `*OPC?` and `*WAI` that mean something

**Problem.** Every command completes synchronously, so `*OPC` sets OPC at once,
`*OPC?` answers `1` at once and `*WAI` does nothing (README, Limitations). An
instrument with a slow operation (a measurement, a relay settle, a sweep) has no
way to tell the controller when it is done, and cannot block the main loop
waiting either.

**Change.** Design and implement pending operations. Requirements, not a
prescribed API:

- A callback can start an operation and return; the application later reports
  it complete (e.g. `viper_scpi_op_begin(ctx)` / `viper_scpi_op_end(ctx)`, or a
  count of pending operations).
- `*OPC` sets the OPC bit only when no operation is pending (immediately if
  none, otherwise on completion), and that completion must raise a service
  request edge.
- `*OPC?` queues its `1` only on completion; in the message-exchange model the
  reply simply is not ready (MAV clear) until then.
- `*WAI` suspends execution of the rest of the message until completion. This
  must not block: keep the unexecuted units and resume them from a poll
  function the transport calls (e.g. `viper_scpi_poll(ctx)`), and refuse new
  input meanwhile in a way a transport can use for flow control.
- Device clear cancels the wait and the deferred `*OPC`/`*OPC?` (check the
  IEEE 488.2 device-clear requirements for the exact rule on pending `*OPC`).

**Done when** the design is written up in the README first, then tests cover
each bullet, including a `*WAI` in the middle of a line, an operation finishing
during `*OPC?`, and device clear while waiting.

### 2.2 The SCPI STATus subsystem

**Problem.** SCPI-99 requires the STATus subsystem (OPERation and QUEStionable
register sets and `STATus:PRESet`), and their summaries belong in status byte
bits 7 (OPER) and 3 (QUES). None of it exists, so an instrument cannot report
"measuring", "overload", "temperature out of range" in the standard way.

**Change.**
- Context: condition, event, enable, positive and negative transition registers
  for OPERation and QUEStionable (16 bits each; about 20 bytes total).
- Built-in tree: `STATus:OPERation[:EVENt]?`, `:CONDition?`, `:ENABle[?]`,
  `:PTRansition[?]`, `:NTRansition[?]`, the same for `QUEStionable`, and
  `STATus:PRESet`.
- API for the instrument: `viper_scpi_status_condition(ctx, reg, mask, value)`
  that applies the transition filters, latches events, and updates the summary
  bits — and therefore the service-request edge tracking.
- `*CLS` clears the event registers (not conditions or enables).

**Done when** tests cover each query, the transition filters in both
directions, event latching and clearing on read, the summary bits in `*STB?`,
SRQ edges from a QUES event with `*SRE 8`, `*CLS`, and `STAT:PRES`.

### 2.3 Streaming block input for large uploads

**Problem.** A block parameter must fit in `VIPER_SCPI_BUFFER_SIZE` together
with the rest of the line, or the line is dropped with `-363`. Instruments that
accept waveforms, firmware images or files (`MMEM:DATA`, `TRAC:DATA`) need
blocks far larger than RAM. (The output side already has
`viper_scpi_result_block_fill()`; this is its input counterpart.)

**Change.** Let a node opt in to receiving its block in chunks, e.g. a separate
callback in the node or a flag plus `viper_scpi_block_read()`. The header and the
parameters before the block must be parsed and the command resolved *before*
the block data arrives, so the input state machine has to recognise "header
complete, block starting" for such nodes. Define what happens on: EOM before the
declared length (ties in with item 1.2), device clear mid-block, a parameter
after the block, and a node that is not streaming receiving a large block
(unchanged: `-363`).

**Done when** a test uploads a block several times larger than the buffer in
random chunk sizes and verifies every byte, plus tests for each case above.

### 2.4 Device-specific error texts and additional information

**Problem.** `viper_scpi_error_message()` knows only the built-in codes;
anything else reads `"Device-specific error"`. SCPI-99 also allows appending
detail (`-222,"Data out of range;VOLT 31 exceeds 30"`), which helps users a lot,
and there is no way to supply it.

**Change.**
- Config field: a `{ code, text }` table for the instrument's own codes, searched
  before the built-in one.
- `viper_scpi_error_push_info(ctx, code, info)`: `info` must point to static
  storage (no copying, no buffer per entry); `SYSTem:ERRor?` prints
  `<code>,"<text>;<info>"`. That costs one pointer per error-queue entry; state
  the context-size cost.

**Done when** tests cover user codes, built-in codes not shadowed unless the
table lists them, info text with embedded quotes (doubled), and queue overflow
keeping `-350` without info.

### 2.5 `INFinity`, `NINFinity`, `NAN`, `UP`, `DOWN`

**Problem.** Listed under Limitations. `NUM INF` is `-104` today.

**Change.** Accept `INFinity` / `NINFinity` / `NAN` in the double parsers,
mapping to ±INF/NaN. For `UP`/`DOWN`, add a variant of the `_kw` parsers that
reports which keyword was given so the instrument can step its own value.

**Done when** tested for each keyword, short and long forms, and their
interaction with MIN/MAX/DEF and range checks (INF must not pass a finite
`[min, max]`).

### 2.6 Optional leniencies (default off)

Real host software is not always standard-conformant. Two cases worth offering
as configuration flags, **off by default**:

- *Relative path fallback.* `SOUR:VOLT 1;SOUR:CURR 2` is `-113` for the second
  unit under SCPI-99 §6.2.4 (it means `SOUR:SOUR:CURR`). Some host programs
  send exactly that. Flag: on failure to resolve relative to the path, retry
  from the root.
- *EOM-delimited messages.* In the message-exchange model, `*IDN?<LF>*OPC?<LF>`
  in one transport message is two program messages, so the second interrupts
  the first (`-410`). Flag: treat LF as a unit separator and only EOM as the
  message end, joining the responses with `;`.

**Done when** each flag has tests with it on and off, and the README states that
the default is the standard behaviour.

---

## 3. Footprint and performance

### 3.1 Drop the dependence on `snprintf` (and `strtod`)

**Measured** in the MSPM0 firmware (TI Arm Clang 5.1.1, `-O2`, Cortex-M0+,
linker map module summary): the library is 9.6 KB code + 0.9 KB rodata, and
libc contributes another ~7 KB to the image for formatted output
(`_printfi` 4.6 KB, plus `snprintf`, `frexp`, `scalbn`, `divdf3`). Integer
results alone pull in the full `printf` engine. On a 128 KB part that is a lot
for printing `*STB?`.

**Change.**
- Format integers with a small local routine (no `snprintf`) — this alone should
  let an application that does not use doubles drop `printf` entirely.
- Keep doubles behind the same `result_double` API but make the float formatter
  and `strtod` use separable, so an application that never calls the double
  APIs links neither (check with `--gc-sections` / TI's unused-section
  elimination, which already removes uncalled functions).
- If a local float formatter is written, it must meet item 1.1 and round-trip
  with the parser.

**Done when** the `cortex-m` CI job reports per-object sizes and a link test
(small `main` using only integer results) shows no `_printfi`/`vfprintf` in the
`arm-none-eabi-gcc` map; numbers before/after go in the commit message.

### 3.2 Avoid 64-bit arithmetic on 32-bit targets

`parse_integer()` accumulates in `uint64_t` and checks overflow with a 64-bit
division, which brings in `__aeabi_uldivmod` / `__udivmoddi4` on Cortex-M0.
Use a 32-bit accumulator with a multiply-overflow check against
`UINT32_MAX / base`, keeping identical results (the existing tests define them).

### 3.3 Small things

- `viper_scpi_pattern_match()` calls `strlen(pattern)` on every attempt during
  tree resolution; the node patterns are constant, so lengths can be computed
  while scanning instead.
- `error_texts` is searched linearly; fine at this size, but if item 2.4 grows
  it, keep built-ins sorted or grouped by class.

Measure before claiming a speed-up; do not trade clarity for unmeasured gains.

---

## 4. Project hygiene

### 4.1 Versions, tags and a changelog

Projects vendor this library by commit and record it (`VENDOR.md`). Make that
easier and safer:

- `VIPER_SCPI_VERSION_MAJOR/MINOR/PATCH` and a string in `viper_scpi.h`, kept in
  step with `project(... VERSION ...)` in `CMakeLists.txt`.
- `CHANGELOG.md` with a section per release; mark breaking changes. The first
  entry should record the message-exchange model and PON-at-init as the changes
  since 0.1.0.
- Tag releases (`v0.2.0`, ...) so users can pin a tag instead of a hash.

### 4.2 CMake friendliness for embedding

The tests and examples options default to `ON` even when the library is pulled
in with `add_subdirectory()`, so every user has to force them off (the MSPM0
firmware does). Default them to `PROJECT_IS_TOP_LEVEL` (CMake ≥ 3.21; keep the
3.10 minimum working with a fallback). Optionally add `install()`/export rules.

### 4.3 Fuzz the message-exchange model

`tests/test_fuzz.c` only drives the stream model. Add a second fuzz loop with an
output buffer that interleaves random `viper_scpi_input_buffer()`,
`viper_scpi_end_of_message()`, `viper_scpi_output_read()` with random sizes,
`viper_scpi_output_discard()`, `viper_scpi_device_clear()` and
`viper_scpi_take_service_request()`, asserting invariants: reads never exceed
what was asked; after `end` the queue is empty and MAV is clear; MAV in the
status byte always equals `viper_scpi_output_pending()`. Must pass under
ASan/UBSan.

### 4.4 CI maintenance

The last run warned that `actions/checkout@v4` targets the deprecated Node.js 20
and that `ubuntu-latest` is moving to Ubuntu 26. Move to the current major of
`actions/checkout`, and confirm the jobs still pass on the new image.

### 4.5 Retire the original specification

`viper_scpi_specification.md` (now in `docs/history/`) is the prompt the library was first generated
from. It is out of date (no message-exchange model, no units, no suffixes) and a
reader cannot tell which document is authoritative. Move it to
`docs/history/` with a note at the top, and make the README the reference.

---

## 5. Found after 0.2.0

### 5.1 A message that ends right after a block's `#` still runs the command

> **Status:** fixed in 0.2.1.

**Problem.** Item 1.2 drops a message that ends inside a definite-length block,
but only once the length digit after `#` has arrived (`BLK_LENGTH`/`BLK_DATA`).
A message that ends on the `#` itself (`BLK_HASH`) is executed with `#` as the
parameter. Measured on the MSPM0 USBTMC firmware (v0.2.0, each sent with
`write_raw`, so EOM follows the last byte; `LED` takes a
`viper_scpi_param_choice()`):

| sent | errors queued | command ran? |
| --- | --- | --- |
| `LED #` | `-224,"Illegal parameter value"` | yes, rejected `#` itself |
| `LED #1` | `-161,"Invalid block data"` | no |
| `LED #12` | `-161,"Invalid block data"` | no |
| `LED #12a` | `-161,"Invalid block data"` | no |
| `LED #0abc` | `-224,"Illegal parameter value"` | yes, correctly: `#0` is a complete indefinite block |

So the controller sees an error that blames the parameter's value instead of
the truncated block. A command that accepts raw or string parameters would not
reject `#` at all and would act on it.

Cause (`src/scpi_parser.c`): in `viper_scpi_input_char()`, `case BLK_HASH`
treats anything other than `0`..`9` as an ordinary character
(`block_state = BLK_NONE`), which is right for `#H`/`#Q`/`#B` non-decimal
numbers but also lets the line terminator through. `finish_line()` then sees
`BLK_NONE` and executes the line. Reading the code, the same happens on
stream transports (`LED #<LF>`) and, with `VIPER_SCPI_OPT_EOM_MESSAGES`, at an
LF separator; only the EOM case was measured.

**Change.**
- A message ending while `block_state == BLK_HASH` is treated like item 1.2:
  push **-161**, do not run the line, reset the input. Cover all three ways a
  message can end: `viper_scpi_end_of_message()`, LF on a stream transport,
  and (for the unit only) LF under `VIPER_SCPI_OPT_EOM_MESSAGES`.
- Decide, from IEEE 488.2 §7.7.4 (non-decimal numeric) and §7.7.6 (arbitrary
  block), what a `#` followed by a unit or parameter terminator (`;`, `,`,
  whitespace) is. It begins no valid program data element, so it should not
  reach the callback as text either; pick the error code (-161, -102 or
  -104), justify it in the commit message, and apply it to these cases too.
  Leave `#H`, `#Q`, `#B` and every other `#`+letter sequence as they are today:
  they go to the callback, whose parsers handle or reject them.
- Streaming nodes (item 2.3) are unaffected: streaming starts only after the
  length digits are complete.

**Done when** tests in both models show: `CMD #` + end of message → `-161`,
callback not called, the next message unaffected, a queued reply handled as
for any other new message (`-410` rules unchanged); the `;`/`,`/whitespace
decision is tested and documented in the README's error table; `#HFF`,
`#B101`, `#Q17`, `#0...` and complete definite blocks behave as before; the
message-exchange fuzz loop (item 4.3) still passes under ASan/UBSan. Record the
behaviour change in `CHANGELOG.md` under Unreleased as a fix.

---

## 6. Found after 0.4.0

Both from moving the MSPM0 USBTMC firmware from 0.2.1 to 0.4.0 (2026-11-03).
The upgrade itself was one line and passed the 57-item hardware test; these
are costs and traps the upgrade showed, not failures.

### 6.1 `viper_scpi_param_integer()` links the unit parser even without units

**Problem.** The firmware's only number parameter is `TEST:DATA? <n>`, a
spec with `units = NULL`:

```c
static const viper_scpi_integer_t size = { .min = 0, .max = 100000000, .def = 1000 };
```

Yet the link map (TI Arm Clang 5.1.1, `-O2`, `--gc-sections` via the static
archive) contains all of `scpi_units.c`: 956 bytes of code
(`viper_scpi_number_unit` 712, `viper_scpi_scan_number` 244) and the 72-byte
`prefixes` table. None of it can run: `integer_value()` in
`src/scpi_params.c` takes the `spec->units == NULL` branch
(`parse_integer()`), but the other branch calls `viper_scpi_number_unit()`,
so the reference is there and the linker keeps the object. On this Cortex-M0+
that is about 40 % of the library's 2.3 KB growth from 0.2.1 to 0.4.0, and it
lands on exactly the integer-only firmware that `param_integer` was added for.
`docs/configuration.md` only promises that `scpi_float.c` stays out; the
unit code is not mentioned either way.

**Change.** Make the unit parser reachable only from data that a caller
names. One way: let the unit table carry it (each `viper_scpi_unit_t` table,
or a small wrapper struct the `units` field points to, holds a pointer to the
parsing function), so `integer_value()` calls through `spec->units` and never
names `viper_scpi_number_unit()`; the unit tables live in `scpi_units.c` and
pull it in only when one of them is referenced. A separate entry point
(`viper_scpi_param_integer_unit()`) would also do, at the cost of a second
function for the same job, which 0.4.0 just removed. Pick one; do not grow the
context or the spec for it.

**Done when** the `cortex-m` CI link test has a case that calls
`viper_scpi_param_integer()` with `units = NULL` only and fails if
`viper_scpi_number_unit` (or anything else from `scpi_units.c`) is in the
image, next to the existing "units link no soft-float" case;
`docs/configuration.md` says which parsers pull in `scpi_units.c`; the
behaviour with units is unchanged (`tests/test_units.c`).

### 6.2 The two spec structs order their fields differently, and the docs initialise them by position

**Problem.**

```c
typedef struct { double  min, max, def; const viper_scpi_unit_t *units; uint8_t flags; } viper_scpi_number_t;
typedef struct { int32_t min, max, def; int16_t exp10; uint8_t flags; const viper_scpi_unit_t *units; } viper_scpi_integer_t;
```

`units` is fourth in one and last in the other. With positional
initialisers, which is how the README, `docs/getting-started.md`,
`docs/parameters.md`, the doc comments in `viper_scpi.h` and the tests write
them (25 places), moving a spec from one parser to the other means
reordering it by hand. Getting it wrong is not always diagnosed:
`{ 0, 30000, 0, viper_scpi_units_volt, 0 }` written for the integer spec is a
pointer-to-integer conversion that some compilers only warn about. The
upgrade guide given for 0.4.0 used the positional form too; the MSPM0
firmware uses designated initialisers to stay clear of it. The ground rules
above already ask for designated initialisers for configuration structs;
the specs are the same kind of thing.

**Change.**
- Write every spec in the README, the docs, the `viper_scpi.h` comments and
  the examples with designated initialisers
  (`{ .min = 0, .max = 30000, .exp10 = -3, .units = viper_scpi_units_volt }`).
  This is the user-visible fix and is not breaking. Tests may stay positional
  where that is clearer, but at least one test per struct should use the
  designated form so the documented spelling compiles in CI (strict-c99
  and the C++ consumer check; designated initialisers in C++ need C++20, so
  check what that job builds as and say in the docs what C++ users write).
- Optionally make the order agree (`exp10` last in `viper_scpi_integer_t`, so
  both start `min, max, def, units, flags`). That is **Breaking** for
  positional users, and only partly diagnosed (pointer ↔ integer), so do it
  only together with the docs change and name it in `CHANGELOG.md`. If the
  order stays, say in the header why the two differ.

**Done when** no example a user would copy (README, `docs/`, header comments,
`examples/`) initialises a spec by position, the designated form is
compiled in CI, and the order decision is recorded (in the header comment if
kept, under **Breaking** in `CHANGELOG.md` if changed).
