#!/bin/sh
# Link tests for items 3.1 and 6.1: Cortex-M0+ programs that use only
# integers must not pull in the C library's printf or strtod machinery, nor
# soft-float helpers, and without a unit table nothing of scpi_units.c.
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
# Two programs. int_units parses integers with units (millivolts from
# "1500 MV"); int_plain parses integers without any unit table.
cat > "$OUT/common.h" <<'SRC'
#include "viper_scpi/viper_scpi.h"
static size_t out(void *u, const char *d, size_t n) { (void)u; (void)d; return n; }
static const viper_scpi_integer_t count = { .min = 0, .max = 100, .def = 1 };
static int32_t g_count;
static viper_scpi_result_t cmd_count(viper_scpi_context_t *ctx)
{
    if (viper_scpi_is_query(ctx)) {
        viper_scpi_result_int(ctx, g_count);
        return VIPER_SCPI_RES_OK;
    }
    return viper_scpi_param_integer(ctx, 0, &count, &g_count, 0);
}
SRC
cat > "$OUT/int_units.c" <<'SRC'
#include "common.h"
/* Millivolts from "1.5", "1500 MV", MAX: units without floating point. */
static const viper_scpi_integer_t mv = {
    .min = 0, .max = 30000, .units = &viper_scpi_units_volt, .exp10 = -3
};
static int32_t g_mv;
static viper_scpi_result_t cmd_volt(viper_scpi_context_t *ctx)
{
    return viper_scpi_param_integer(ctx, 0, &mv, &g_mv, 0);
}
static const viper_scpi_node_t root[] = {
    { "COUNt", cmd_count, 0 }, { "VOLTage", cmd_volt, 0 }, VIPER_SCPI_NODE_END
};
static const viper_scpi_config_t cfg = { .root = root, .write = out };
static viper_scpi_context_t ctx;
int main(void)
{
    viper_scpi_init(&ctx, &cfg);
    static const char msg[] = "*IDN?;*STB?;COUN 5;COUN?;VOLT 1500 MV;SYST:ERR?\n";
    viper_scpi_input_buffer(&ctx, msg, sizeof msg - 1);
    return 0;
}
SRC
cat > "$OUT/int_plain.c" <<'SRC'
#include "common.h"
static const viper_scpi_node_t root[] = { { "COUNt", cmd_count, 0 }, VIPER_SCPI_NODE_END };
static const viper_scpi_config_t cfg = { .root = root, .write = out };
static viper_scpi_context_t ctx;
int main(void)
{
    viper_scpi_init(&ctx, &cfg);
    static const char msg[] = "*IDN?;COUN 1.5;COUN?;SYST:ERR?\n";
    viper_scpi_input_buffer(&ctx, msg, sizeof msg - 1);
    return 0;
}
SRC
link() {
    # nosys stubs make the linker warn about _write etc.; show its output only on failure.
    if ! arm-none-eabi-gcc $CFLAGS -I"$ROOT/include" "$OUT/$1.c" "$OUT/libviper_scpi.a" \
        --specs=nosys.specs -Wl,--gc-sections -Wl,-Map="$OUT/$1.map" \
        -o "$OUT/$1.elf" 2>"$OUT/$1.log"; then
        cat "$OUT/$1.log" >&2
        exit 1
    fi
    arm-none-eabi-size "$OUT/$1.elf"
    if arm-none-eabi-nm "$OUT/$1.elf" | grep -E ' [Tt] _?(_?s?v?f?printf|_svfprintf_r|_vfprintf_r|_printf_i|strtod|_strtod_r|_dtoa_r)\b'; then
        echo "FAIL: $1 links printf/strtod" >&2
        exit 1
    fi
    if arm-none-eabi-nm "$OUT/$1.elf" | grep -E ' [Tt] (__aeabi_uldivmod|__aeabi_ldivmod|__udivmoddi4|__aeabi_lmul)$'; then
        echo "FAIL: $1 links 64-bit arithmetic helpers" >&2
        exit 1
    fi
    if arm-none-eabi-nm "$OUT/$1.elf" | grep -E ' [Tt] __aeabi_[df](add|sub|mul|div|cmp|2|to)'; then
        echo "FAIL: $1 links soft-float helpers" >&2
        exit 1
    fi
}
link int_units
link int_plain
# Without a unit table, nothing of scpi_units.c (item 6.1): the archive
# member is not loaded at all, so the map never names it.
if grep -n 'scpi_units\.o' "$OUT/int_plain.map"; then
    echo "FAIL: integer parsing without units links scpi_units.o" >&2
    exit 1
fi
if ! grep -q 'scpi_units\.o' "$OUT/int_units.map"; then
    echo "FAIL: the check above would not notice scpi_units.o" >&2
    exit 1
fi
echo "OK: no printf/strtod, soft-float or 64-bit helpers in integer-only programs;"
echo "    scpi_units.o only where a unit table is used"
