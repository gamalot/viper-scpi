# Getting started

[Documentation home](README.md)

## 1. Add the library to your project

viper-scpi is six C files and three public headers. Pick whichever fits the
build system.

**CMake, vendored** (copy or submodule the repository, pinned to a tag):

```cmake
add_subdirectory(third_party/viper-scpi)   # tests and examples are not built
target_link_libraries(firmware PRIVATE viper_scpi::viper_scpi)
```

**CMake, installed:**

```sh
cmake -S viper-scpi -B build && cmake --build build
cmake --install build --prefix /opt/viper-scpi
```

```cmake
find_package(viper_scpi 0.2 REQUIRED)       # with CMAKE_PREFIX_PATH=/opt/viper-scpi
target_link_libraries(firmware PRIVATE viper_scpi::viper_scpi)
```

**Any other build system** (vendor IDE, Make): compile `src/*.c` with
`include/` and `src/` on the include path, and put `include/` on the include
path of your own code. Build the library as a static archive if you can: the
floating-point code is in its own object file and is then linked only if you
use a double API (see [Configuration and footprint](configuration.md)).

Compile-time limits are macros you may override with `-D`, for example
`-DVIPER_SCPI_BUFFER_SIZE=256`. Define them identically for the library and
your own code, since they change the size of the context structure.

## 2. Write a command

A command is a callback that reads parameters and, for a query, writes a
response. It returns `VIPER_SCPI_RES_OK` or an error code, which the library
puts into the error queue.

```c
#include "viper_scpi/viper_scpi.h"

static double g_volts;
static const viper_scpi_number_t volt_spec = {
    .min = 0.0, .max = 30.0, .units = &viper_scpi_units_volt
};

static viper_scpi_result_t cmd_volt(viper_scpi_context_t *ctx)
{
    if (viper_scpi_is_query(ctx)) {                 /* SOUR:VOLT? */
        viper_scpi_result_double(ctx, g_volts);
        return VIPER_SCPI_RES_OK;
    }
    /* SOUR:VOLT 1.5 / 1500 mV / MAX: range-checked, units, keywords */
    return viper_scpi_param_number(ctx, 0, &volt_spec, &g_volts, NULL);
}
```

## 3. Build the command tree

```c
static const viper_scpi_node_t source_nodes[] = {
    { "VOLTage[:LEVel]", cmd_volt, NULL },   /* SOUR:VOLT, SOUR:VOLT:LEV */
    VIPER_SCPI_NODE_END
};

static const viper_scpi_node_t root[] = {
    { "[SOURce]", NULL, source_nodes },      /* SOURce may be omitted: VOLT 3 */
    VIPER_SCPI_NODE_END
};
```

The arrays are `const`, so they stay in Flash. See [Command tree](command-tree.md)
for the pattern syntax.

## 4. Configure and initialise

```c
static size_t uart_write(void *user, const char *data, size_t len)
{
    (void)user;
    for (size_t i = 0; i < len; i++) {
        uart_put(data[i]);                   /* your driver */
    }
    return len;
}

static const viper_scpi_config_t cfg = {
    .root = root,
    .write = uart_write,
    .idn_manufacturer = "Acme",
    .idn_model = "PSU-30",
    .idn_serial = "000123",
    .idn_firmware = "1.0.0",
};

static viper_scpi_context_t scpi;

void scpi_init(void)
{
    if (viper_scpi_init(&scpi, &cfg) != VIPER_SCPI_RES_OK) {
        /* -102: a node pattern is malformed and never matches;
         * -220: the library was built with other VIPER_SCPI_* limits */
    }
}
```

Always use designated initializers for `viper_scpi_config_t`. Fields are added
at the end in new releases, with zero meaning "previous behaviour", so a
designated initializer keeps compiling unchanged.

## 5. Feed it bytes

```c
void uart_rx_isr(void)          /* or a DMA/ring-buffer drain in the main loop */
{
    ring_put(uart_read_byte());
}

void main_loop(void)
{
    for (;;) {
        int c;
        /* While a message waits in *WAI / *OPC?, leave the rest in the
         * ring (bytes fed anyway would be refused with -363). */
        while (!viper_scpi_busy(&scpi) && (c = ring_get()) >= 0) {
            viper_scpi_input_char(&scpi, (char)c);   /* '\n' runs the line */
        }
        viper_scpi_poll(&scpi);                      /* resumes *WAI / *OPC? */
        /* ... the rest of the instrument ... */
    }
}
```

Call the library from one context only -- normally the main loop, not an
interrupt handler. An ISR should queue bytes and set flags.

Now `*IDN?` answers `Acme,PSU-30,000123,1.0.0`, `VOLT 1.5;VOLT?` answers `1.5`,
and `SYST:ERR?` reports any error.

## Next steps

- USBTMC, VXI-11, HiSLIP or GPIB device: read [Transports](transports.md); these
  need the message-exchange model.
- Slow operations (measurements, settling): [Overlapped commands](overlapped-commands.md).
- Reporting instrument state the SCPI way: [Status and errors](status-and-errors.md).
- A complete runnable program: [`examples/bare_metal_demo.c`](../examples/bare_metal_demo.c).
