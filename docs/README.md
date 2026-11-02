# viper-scpi documentation

Guides for using viper-scpi in an instrument. The project
[README](../README.md) is the compact reference; these pages explain how the
pieces fit together and how to integrate them with a real transport.

The pages are plain Markdown kept next to the code and describe the current
`main`. Each release tag carries its own copy of `docs/`, starting with
[`v0.2.1`](https://github.com/gamalot/viper-scpi/tree/v0.2.1/docs); see the
[CHANGELOG](../CHANGELOG.md) for what changed between releases. The
[GitHub wiki](https://github.com/gamalot/viper-scpi/wiki) is generated from
these files (`tools/docs_to_wiki.py`).

| Page | What it covers |
| --- | --- |
| [Getting started](getting-started.md) | Adding the library to a firmware project, a first command, the main loop |
| [Command tree](command-tree.md) | Nodes, long/short forms, optional mnemonics, numeric suffixes, relative paths, built-in commands |
| [Parameters and responses](parameters.md) | Every parameter parser, units, MIN/MAX/DEF/UP/DOWN, strings, blocks, response formatting |
| [Transports](transports.md) | Stream model (UART, TCP) and message-exchange model (USBTMC, VXI-11, HiSLIP, GPIB), with an integration walk-through |
| [Status and errors](status-and-errors.md) | Status byte, ESR, service requests, serial poll, the STATus subsystem, the error queue and error texts |
| [Overlapped commands](overlapped-commands.md) | Slow operations, `*OPC`, `*OPC?`, `*WAI`, `viper_scpi_poll()`, flow control |
| [Large data](large-data.md) | Generated block responses and streaming block input, both larger than RAM |
| [Configuration and footprint](configuration.md) | Compile-time limits, config fields, leniency options, memory and code size |
| [Troubleshooting](troubleshooting.md) | What each error code usually means in practice, and common integration mistakes |
| [Contributing](contributing.md) | Building, testing, CI, the project's rules for changes, releasing |

Other documents:

- [CHANGELOG](../CHANGELOG.md) -- changes per release, breaking ones marked.
- [IMPROVEMENTS](IMPROVEMENTS.md) -- the backlog that produced 0.2.0.
- [history/](history/) -- the original generation prompt, kept for reference only.

## At a glance

viper-scpi is a SCPI / IEEE 488.2 command parser for bare-metal
microcontrollers, in ISO C99:

- **Zero allocation.** One statically allocated context (444 bytes on
  Cortex-M with default limits); command trees live in Flash. No `malloc`, no
  writable static data in the library itself.
- **Non-blocking.** Bytes go in, callbacks run, responses come out. Slow
  operations are handled with `*OPC`/`*WAI` semantics without blocking the
  main loop.
- **Transport-agnostic.** The same library serves a UART console and a USBTMC
  instrument; the transport decides when a message ends and when a reply is
  read.
- **Standard behaviour by default.** Where real host software deviates from
  SCPI-99 or IEEE 488.2, the workaround is an opt-in option.
