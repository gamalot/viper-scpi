#!/bin/sh
# Link test for item 3.1: a Cortex-M0+ program that uses only integer
# responses must not pull in the C library's printf or strtod machinery.
# Usage: .github/linktest.sh [build-dir]   (needs arm-none-eabi-gcc)
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-"$ROOT/build-linktest"}
CFLAGS="-std=c99 -Os -mcpu=cortex-m0plus -mthumb -fno-pic -ffunction-sections -fdata-sections -Wall -Wextra -Werror"
rm -rf "$OUT"
mkdir -p "$OUT"
for f in "$ROOT"/src/*.c; do
    arm-none-eabi-gcc $CFLAGS -I"$ROOT/include" -I"$ROOT/src" -c "$f" -o "$OUT/$(basename "$f" .c).o"
done
arm-none-eabi-ar rcs "$OUT/libviper_scpi.a" "$OUT"/*.o
cat > "$OUT/int_only.c" <<'SRC'
#include "viper_scpi/viper_scpi.h"
static size_t out(void *u, const char *d, size_t n) { (void)u; (void)d; return n; }
static viper_scpi_result_t cmd_count(viper_scpi_context_t *ctx)
{
    int32_t v;
    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_int(ctx, 42);
        return VIPER_SCPI_RES_OK;
    }
    return viper_scpi_param_int32_kw(ctx, 0, 0, 100, 1, &v);
}
static const viper_scpi_node_t root[] = { { "COUNt", cmd_count, 0 }, VIPER_SCPI_NODE_END };
static const viper_scpi_config_t cfg = { .root = root, .write = out };
static viper_scpi_context_t ctx;
int main(void)
{
    viper_scpi_init(&ctx, &cfg);
    viper_scpi_input_buffer(&ctx, "*IDN?;*STB?;COUN 5;COUN?;SYST:ERR?\n", 35);
    return 0;
}
SRC
# nosys stubs make the linker warn about _write etc.; show its output only on failure.
if ! arm-none-eabi-gcc $CFLAGS -I"$ROOT/include" "$OUT/int_only.c" "$OUT/libviper_scpi.a" \
    --specs=nosys.specs -Wl,--gc-sections -Wl,-Map="$OUT/int_only.map" \
    -o "$OUT/int_only.elf" 2>"$OUT/link.log"; then
    cat "$OUT/link.log" >&2
    exit 1
fi
arm-none-eabi-size "$OUT/int_only.elf"
if arm-none-eabi-nm "$OUT/int_only.elf" | grep -E ' [Tt] _?(_?s?v?f?printf|_svfprintf_r|_vfprintf_r|_printf_i|strtod|_strtod_r|_dtoa_r)\b'; then
    echo "FAIL: integer-only program links printf/strtod" >&2
    exit 1
fi
echo "OK: no printf/strtod in an integer-only program"
