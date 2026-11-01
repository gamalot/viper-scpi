> **Historical document -- not maintained.** This is the specification the
> library was first generated from (September 2026, version 0.1.0). It is out
> of date: it predates the message-exchange model, units, numeric suffixes,
> overlapped commands, the STATus subsystem and more. The authoritative
> reference is the [README](../../README.md), together with the comments in
> `include/viper_scpi/viper_scpi.h` and the [CHANGELOG](../../CHANGELOG.md).

# Project Specification: viper-scpi (Bare-Metal SCPI Parser C Library)

## 1. Executive Summary
`viper-scpi` is an ultra-lightweight, zero-allocation, non-blocking SCPI (Standard Commands for Programmable Instruments) parser library written in ISO C (C99). It is explicitly designed for bare-metal microcontrollers without any RTOS or dynamic memory management (`malloc`/`free`). 

It acts as an agnostic protocol layer accepting raw string/byte streams from transport layers (such as USBTMC, LXI / TCP Sockets, or UART) and dispatching recognized commands to user-defined callback functions with clean parameter bindings.

## 2. Key Architecture & Requirements

### 2.1 Transport Agnostic & Non-Blocking Design
- The core engine operates as a state machine processing character streams (`viper_scpi_input_char` or `viper_scpi_input_buffer`).
- No internal polling, blocking loops, or hardware-specific delays.
- Decoupled from transport drivers: USBTMC/LXI/UART feed bytes into `viper-scpi`; `viper-scpi` outputs responses via a user-registered `write` function pointer.

### 2.2 Memory Management (Zero Dynamic Allocation)
- Strictly zero calls to `malloc`, `free`, or Heap usage.
- All command trees, node structures, and token buffers must reside in Flash/ROM or fixed static arrays provided at compile-time or initialization.
- Configurable static limits via C preprocessor macros (e.g., `VIPER_SCPI_MAX_RECURSION_DEPTH`, `VIPER_SCPI_MAX_PARAMS`, `VIPER_SCPI_BUFFER_SIZE`).

### 2.3 SCPI Syntax Features
- **Tree-structured Syntax Support**: Support hierarchical node paths (e.g., `:MEASure:VOLTage:DC?`, `SYSTem:ERRor?`).
- **IEEE 488.2 Mandatory Commands**: Built-in default handlers for `*IDN?`, `*RST`, `*CLS`, `*OPC`, `*OPC?`, `*WAI`, `*STB?`, `*ESR?`.
- **Flexible Command Matching**: 
  - Case-insensitive (e.g., `:MEAS:VOLT:DC?` matches `:measure:voltage:dc?`).
  - Short/Long form recognition (capitalized letters indicate short form).
  - Multi-command execution on a single line separated by semicolons `;`.
- **Parameter Parsing Utilities**:
  - Integer (decimal, hex `0x`, binary `#B`, octal `#Q`).
  - Floating point numbers with scientific notation (`1.23E-3`).
  - Strings (quoted `"..."` or `'...'`).
  - Choice / Enum keywords (e.g., `ON`/`OFF`, `DC`/`AC`).

## 3. Architecture & API Proposal

```
+-------------------------------------------------------+
| Hardware Transports (USBTMC / LXI / UART)            |
+-------------------------------------------------------+
                           |  Raw Bytes
                           v
+-------------------------------------------------------+
| viper-scpi Core Engine (State Machine Parser)         |
|  - Command Tree Node Traversal                        |
|  - Parameter Tokenizer                                |
|  - Error Queue Management (IEEE 488.2)                |
+-------------------------------------------------------+
                           |  Dispatched Callbacks
                           v
+-------------------------------------------------------+
| Application Code (Hardware Control & Measurements)    |
+-------------------------------------------------------+
```

### Key C Data Structures & Types
```c
typedef enum {
    VIPER_SCPI_RES_OK = 0,
    VIPER_SCPI_RES_ERR_UNKNOWN_CMD = -100,
    VIPER_SCPI_RES_ERR_PARAM_INVALID = -220,
    // IEEE 488.2 standard error codes
} viper_scpi_result_t;

typedef struct viper_scpi_context_s viper_scpi_context_t;

// User callback prototype
typedef viper_scpi_result_t (*viper_scpi_cmd_callback_t)(viper_scpi_context_t *ctx);

// Command Tree Node definition (stored in Flash/const)
typedef struct viper_scpi_node_s {
    const char *pattern;                 // e.g., "VOLTage"
    viper_scpi_cmd_callback_t callback;  // NULL if it's an intermediate hierarchy node
    const struct viper_scpi_node_s *children;
} viper_scpi_node_t;
```

## 4. Repository Structure & Deliverables
The generated repository should follow a clean CMake-based embedded library layout:

```text
viper-scpi/
├── CMakeLists.txt              # Build configuration with test target
├── include/
│   └── viper_scpi/
│       ├── viper_scpi.h        # Public API
│       ├── scpi_config.h       # Default limits & macros
│       └── scpi_types.h        # Data structures
├── src/
│   ├── scpi_parser.c           # Core SCPI state machine
│   ├── scpi_ieee488.c          # Mandatory *IDN?, *RST etc.
│   ├── scpi_params.c          # Parameter extraction helpers
│   └── scpi_error.c           # SCPI Error Queue implementation
├── tests/
│   ├── CMakeLists.txt
│   └── test_parser.c           # Unity / CMocka / CTest suite
├── examples/
│   └── bare_metal_demo.c       # Minimal UART/USBTMC simulation loop
└── README.md                   # API usage, command tree registration guide
```

## 5. Verification & Acceptance Criteria
1. **Compilation**: Standard C99 clean build with zero warnings under `-Wall -Wextra -Wpedantic`.
2. **Unit Testing**: All parser logic, parameter conversions, and error handling must pass automated CTest suites in CI/Cloud Sessions.
3. **Memory footprint check**: Verify through static analysis or build sizes that stack and global BSS usage are minimal (< 2KB total static memory footprint).