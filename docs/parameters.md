# Parameters and responses

[Documentation home](README.md)

## Reading parameters

Parameters are read inside the callback, by zero-based index. Every parser
returns `VIPER_SCPI_RES_OK` or an SCPI error code you can return directly:

```c
static viper_scpi_result_t cmd_ramp(viper_scpi_context_t *ctx)   /* RAMP <start>,<stop>[,<steps>] */
{
    static const viper_scpi_integer_t steps_spec = { .min = 1, .max = 1000, .def = 10 };
    double start, stop;
    int32_t steps = 10;
    viper_scpi_result_t res;

    if ((res = viper_scpi_param_double(ctx, 0, &start)) != VIPER_SCPI_RES_OK ||
        (res = viper_scpi_param_double(ctx, 1, &stop)) != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (viper_scpi_param_present(ctx, 2) &&
        (res = viper_scpi_param_integer(ctx, 2, &steps_spec, &steps, NULL)) != VIPER_SCPI_RES_OK) {
        return res;
    }
    if (viper_scpi_param_count(ctx) > 3) {
        return VIPER_SCPI_RES_ERR_PARAM_NOT_ALLOWED;
    }
    start_ramp(start, stop, steps);
    return VIPER_SCPI_RES_OK;
}
```

Errors the parsers return, with the same meaning everywhere:

| Code | When |
| --- | --- |
| -109 Missing parameter | the index is beyond the last parameter |
| -104 Data type error | the text is not of the requested kind |
| -222 Data out of range | a number outside the type or the given `[min, max]` |
| -224 Illegal parameter value | an unknown choice keyword; NAN in a range-checked parser |
| -131 Invalid suffix | an unknown or disallowed unit |
| -220 Parameter error | a programming error: NULL spec or output pointer, `min > max`, UP/DOWN allowed without a `kw` pointer |

Callers never see -102 Syntax error from the parsers: malformed parameter
lists (`A 1,,2`, an unterminated string) are rejected before the callback
runs.

## Parsers

| Function | Accepts | Notes |
| --- | --- | --- |
| `viper_scpi_param_number` | a double per a const `viper_scpi_number_t` spec: range, default, units, `MIN`/`MAX`/`DEF`, optionally `UP`/`DOWN` | the general one; with units the result is in base units, scaled by an exact power of ten (`10US` equals `10E-6`) |
| `viper_scpi_param_integer` | an integer per a const `viper_scpi_integer_t` spec: the same, with the result a count of 10^`exp10` base units (`1.5` or `1500 MV` give 1500 for mV) | no floating point; units with a scale other than 1 (`MIN`, `HOUR`) are refused |
| `viper_scpi_param_int32` | `42`, `-7`, `+3`, `1.5`, `3.2E1`, `0x1F`, `#HFF`, `#B1010`, `#Q17` | a decimal number is rounded half away from zero (`1.5` is 2, `-0.5` is -1), in integer arithmetic; sign only on decimal and `0x` |
| `viper_scpi_param_uint32` | as above, 0..4294967295 | `-0` is accepted |
| `viper_scpi_param_double` | `1.5`, `-.5`, `1E-3`, `2.5e+3`, `INF`, `NINF`, `NAN` | at least one digit; no hex |
| `viper_scpi_param_bool` | `ON`, `OFF`, or a number | a number is rounded, non-zero is ON (SCPI-99 7.3) |
| `viper_scpi_param_choice` | a keyword from your list, long or short form | value from the list |
| `viper_scpi_param_string` | `"text"` or `'text'`, doubled quote as escape | copied, NUL-terminated; on an error `out` is left unchanged |
| `viper_scpi_param_block` | `#<n><len><bytes>` or `#0<bytes>` | points into the line buffer |
| `viper_scpi_param_raw` | anything | the token as received |

`viper_scpi_param_count()` and `viper_scpi_param_present()` tell how many
parameters there are; optional parameters are simply absent.

### Keywords: MIN, MAX, DEF, UP, DOWN

```c
static const viper_scpi_number_t volt = { .min = 0.0, .max = 30.0, .def = 5.0 };
double volts;
res = viper_scpi_param_number(ctx, 0, &volt, &volts, NULL);
```

`MIN`/`MINimum`, `MAX`/`MAXimum` and `DEF`/`DEFault` resolve to the spec's
limits; numbers must lie within them. Queries use the same spec:
`VOLT? MAX` answers 30.

`UP` and `DOWN` are accepted only with `VIPER_SCPI_NUM_UPDOWN` in the spec's
flags. The `kw` argument is then required: it says which keyword (if any) was
given, and the output is left unchanged for UP/DOWN:

```c
static const viper_scpi_number_t volt_step = {
    .min = 0.0, .max = 30.0, .def = 5.0, .flags = VIPER_SCPI_NUM_UPDOWN
};
viper_scpi_numeric_kw_t kw;
double v = g_volts;
res = viper_scpi_param_number(ctx, 0, &volt_step, &v, &kw);
if (res == VIPER_SCPI_RES_OK) {
    if (kw == VIPER_SCPI_KW_UP)   v += g_step;
    if (kw == VIPER_SCPI_KW_DOWN) v -= g_step;
    if (v < 0.0 || v > 30.0) return VIPER_SCPI_RES_ERR_OUT_OF_RANGE;
    g_volts = v;
}
```

`viper_scpi_param_integer()` takes the same keywords and flag.

### Infinity and NaN

The double parsers accept `INFinity`, `NINFinity` and `NAN`. In range-checked
parsers an infinity must lie within `[min, max]` (pass `HUGE_VAL` as `max` to
allow it) and NAN always fails with -224. Responses encode them as SCPI
requires: `9.9E+37`, `-9.9E+37`, `9.91E+37`.

### Units

```c
static const viper_scpi_number_t freq = {
    .min = 1.0, .max = 1e9, .def = 1e3, .units = &viper_scpi_units_hertz
};
double hz;
res = viper_scpi_param_number(ctx, 0, &freq, &hz, NULL);   /* 10MHZ -> 1e7 */
```

- Multipliers: `EX` 1e18, `PE` 1e15, `T` 1e12, `G` 1e9, `MA` 1e6, `K` 1e3,
  `M` 1e-3, `U` 1e-6, `N` 1e-9, `P` 1e-12, `F` 1e-15, `A` 1e-18. Case does not
  matter, so `mV` and `MV` are both millivolts; megavolts are `MAV`.
- Exception required by SCPI: for Hz and ohm, `M` means mega (`MHZ`, `MOHM`).
- Space between number and unit is allowed (`100 mV`). A bare number is taken
  as base units.
- Ready-made tables: `viper_scpi_units_volt`, `_amp`, `_hertz`, `_ohm`,
  `_watt`, `_farad`, `_henry`, `_decibel`, `_second` (S, SEC, MIN, HOUR),
  `_celsius`.

Your own table: a unit list ending with a NULL name, wrapped by
`VIPER_SCPI_UNIT_TABLE()`, which adds the unit parser. Only tables reach that
parser, so firmware that uses none does not link `src/scpi_units.c`.

```c
static const viper_scpi_unit_t pressure_list[] = {
    { "PA",  1.0,      0 },                          /* KPA, MPA, ... */
    { "BAR", 1e5,      VIPER_SCPI_UNIT_NO_PREFIX },
    { "PSI", 6894.757, VIPER_SCPI_UNIT_NO_PREFIX },
    { NULL, 0, 0 }
};
static const viper_scpi_unit_table_t pressure = VIPER_SCPI_UNIT_TABLE(pressure_list);
static const viper_scpi_number_t gauge = { .min = 0.0, .max = 1e7, .units = &pressure };
```

Conversion is multiplicative only; a unit with an offset (degF) cannot be
expressed.

### Choices

```c
static const viper_scpi_choice_t trigger_sources[] = {
    { "IMMediate", 0 }, { "BUS", 1 }, { "EXTernal", 2 }, { NULL, 0 }
};
int src;
res = viper_scpi_param_choice(ctx, 0, trigger_sources, &src);   /* -224 if unknown */
```

### Strings

```c
char name[32];
size_t len;
res = viper_scpi_param_string(ctx, 0, name, sizeof name, &len);
```

`"say ""hi"""` gives `say "hi"`. A string that does not fit `name` (including
its NUL) is -222; an unquoted token is -104. In the message-exchange model a
string may contain line feeds (see [Transports](transports.md)).

### Blocks

```c
const uint8_t *data;
size_t len;
res = viper_scpi_param_block(ctx, 0, &data, &len);
```

`data` points into the line buffer and is valid only during the callback. The
whole program message, block included, must fit in `VIPER_SCPI_BUFFER_SIZE`;
for larger uploads see [Large data](large-data.md).

### Expression data and channel lists

A parenthesised parameter (IEEE 488.2 7.7.7 expression data), such as the SCPI
channel list `(@1,3:5)`, is one parameter: commas inside parentheses do not
separate parameters. Read it with `viper_scpi_param_raw()` and parse the list
yourself; `ROUT:CLOS (@1,2,3),5` has two parameters.

## Writing responses

Call the `viper_scpi_result_*` functions from a query callback, in order.
Values from one command are separated by `,`, responses of different commands
in one message by `;`, and the message ends with `\n` -- all added for you.

| Function | Output |
| --- | --- |
| `viper_scpi_result_int(ctx, -42)` | `-42` |
| `viper_scpi_result_uint(ctx, 42u)` | `42` |
| `viper_scpi_result_bool(ctx, true)` | `1` |
| `viper_scpi_result_double(ctx, 1.5e-7)` | `1.5E-07` (15 significant digits) |
| `viper_scpi_result_double_digits(ctx, v, 7)` | 7 significant digits |
| `viper_scpi_result_text(ctx, "VOLT")` | `VOLT` (character data, unquoted) |
| `viper_scpi_result_string(ctx, "a\"b")` | `"a""b"` |
| `viper_scpi_result_block(ctx, buf, 5)` | `#15` followed by the 5 bytes |
| `viper_scpi_result_block_fill(ctx, n, fill, user)` | a block produced on demand, see [Large data](large-data.md) |

Example: `MEAS:VOLT?;CURR?` with two callbacks each writing one double gives
`12.5;0.25\n`.

Fifteen significant digits means any value with up to 15 digits reads back
exactly as written. Use `viper_scpi_result_double_digits()` to report only the
digits an instrument actually resolves, or 17 for a lossless binary round trip.
