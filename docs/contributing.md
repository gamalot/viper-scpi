# Contributing

[Documentation home](README.md)

## Build and test

```sh
cmake -S . -B build -DVIPER_SCPI_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

With sanitizers (CI uses clang; GCC works the same way):

```sh
cmake -S . -B build-asan -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Debug -DVIPER_SCPI_WERROR=ON \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all"
cmake --build build-asan && ctest --test-dir build-asan --output-on-failure
```

The other CI checks are short shell steps in
[`.github/workflows/ci.yml`](../.github/workflows/ci.yml); run them the same way
locally. The Cortex-M link test is a script: `sh .github/linktest.sh build-linktest`
(needs `arm-none-eabi-gcc` and newlib).

## Tests

| Suite | Covers |
| --- | --- |
| `test_parser` | line assembly, headers, paths, separators, limits |
| `test_params` | every parameter parser, response formatting |
| `test_patterns` | multi-mnemonic patterns, optional parts, suffixes |
| `test_units` | units and multipliers |
| `test_extras` | optional nodes, suffixes, blocks, MIN/MAX/DEF |
| `test_ieee488` | common commands and status registers |
| `test_error` | error queue, `SYSTem:ERRor`, error texts and detail |
| `test_status` | the STATus subsystem |
| `test_message` | the message-exchange model |
| `test_overlapped` | `*OPC`, `*OPC?`, `*WAI`, `viper_scpi_poll()` |
| `test_stream` | streaming block input |
| `test_options` | leniency options |
| `test_fuzz` | deterministic fuzzing of both models with invariants and coverage counters |

The suites use a tiny assertion layer in `tests/test_util.h` and a shared
fixture in `tests/test_fixture.c`; no external framework. Add a suite by adding
its name to the list in `tests/CMakeLists.txt`.

## Rules for changes

The project keeps these properties, and CI checks most of them:

- ISO C99, no dynamic allocation, no blocking, no writable static data in the
  library; state goes in `viper_scpi_context_t`.
- The context stays under 2 KB with default limits; say in the commit message
  how much a change adds.
- Warning-free under every CI configuration, including
  `-std=c99 -pedantic-errors -Wshadow -Wconversion`, sanitizers and clang-tidy.
- Every behaviour change has a test, and every new public function is
  documented in `viper_scpi.h`, the README and, where it matters, these pages.
- New `viper_scpi_config_t` fields go at the end, and zero must mean the
  previous behaviour. Examples and tests use designated initializers.
- Standard behaviour (IEEE 488.2, SCPI-99) is the default. Workarounds for
  non-conforming hosts are opt-in options.
- Integer-only firmware must not link printf/strtod: keep floating-point code
  in `src/scpi_float.c`.
- Breaking changes are named in the commit message and in the CHANGELOG.

One commit per logical change; the commit message says what was wrong, what
changed, how it was verified, and any size impact.

## Releasing

1. Move the `Unreleased` section of [CHANGELOG.md](../CHANGELOG.md) under a new
   version heading with the date, and add its compare link at the bottom.
2. Bump `VIPER_SCPI_VERSION_MAJOR/MINOR/PATCH` and `VIPER_SCPI_VERSION_STRING`
   in `include/viper_scpi/viper_scpi.h` and `project(... VERSION ...)` in
   `CMakeLists.txt` together; CMake refuses to configure if they differ.
3. Commit, wait for CI to pass, then tag and push the tag:
   `git tag -a vX.Y.Z -m "viper-scpi X.Y.Z" && git push origin vX.Y.Z`.
4. Optionally publish a GitHub release from the tag with the changelog section
   as its notes.

While the major version is 0, a minor release may break the API; the
changelog says how.
