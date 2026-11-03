# Command tree

[Documentation home](README.md)

## Nodes

A tree is a `const` array of nodes, each `{ pattern, callback, children }`,
ending with `VIPER_SCPI_NODE_END`:

```c
static const viper_scpi_node_t meas_volt_nodes[] = {
    { "DC", cmd_meas_dc, NULL },
    { "AC", cmd_meas_ac, NULL },
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t meas_nodes[] = {
    { "VOLTage", NULL, meas_volt_nodes },    /* hierarchy only */
    VIPER_SCPI_NODE_END
};
static const viper_scpi_node_t root[] = {
    { "MEASure", NULL, meas_nodes },
    { "SYSTem:BEEPer", cmd_beep, NULL },     /* several mnemonics in one node */
    VIPER_SCPI_NODE_END
};
```

- `callback` is NULL for a node that only groups others. A node may have a
  callback *and* children (`SYSTem:ERRor?` and `SYSTem:ERRor:NEXT?` both exist).
- `children` is NULL for a leaf.
- The callback serves both the command and the query form; it tells them apart
  with `viper_scpi_is_query()`. Start it with
  `viper_scpi_expect(ctx, set_params, query_params)`: it returns -113 for a
  form the command does not have (`VIPER_SCPI_NO_FORM`) and -108 for more
  parameters than that form takes, as the built-ins do:

  ```c
  viper_scpi_result_t res = viper_scpi_expect(ctx, VIPER_SCPI_NO_FORM, 0); /* MEAS? only */
  if (res != VIPER_SCPI_RES_OK) {
      return res;
  }
  ```

## Long and short forms

The leading run of upper-case letters (and digits) is the short form, the
whole word the long form. Matching ignores case. `"VOLTage"` accepts `VOLT`,
`volt`, `VOLTAGE`, `Voltage`, and rejects `VOL` and `VOLTA` -- SCPI allows only
the two forms. A pattern written all in capitals (`"DC"`) has one form.

## Pattern syntax

| Pattern | Matches |
| --- | --- |
| `"VOLTage"` | `VOLT`, `VOLTAGE` |
| `"VOLTage[:LEVel]"` | `VOLT`, `VOLT:LEV` |
| `"TRIGger[:SEQuence]:SOURce"` | `TRIG:SOUR`, `TRIG:SEQ:SOUR` |
| `"[SENSe]"` (with children) | the children with or without `SENS:` in front |
| `"CHANnel#"` | `CHAN`, `CHAN1`, `CHANNEL12` |
| `"CALCulate#[:MATH]:STATe"` | `CALC:STAT`, `CALC2:MATH:STAT` |

- Brackets mark an optional mnemonic; they do not nest.
- When a node's mnemonics are all optional and the header ends at that node's
  parent, the node is taken by default: with `POWer` -> `[LEVel]` (callback),
  `POW?` runs the `LEVel` callback.
- Children continue after the whole pattern. With
  `{ "ROUTe:LAYer[:SUB]", cb, layer_nodes }`, `ROUT:LAY:COUN?` and
  `ROUT:LAY:SUB:COUN?` both reach `COUNt` in `layer_nodes`.
- A malformed pattern (nested or unclosed brackets, an empty mnemonic) never
  matches. `viper_scpi_init()` returns -102 for it (the context is
  initialised all the same), so check what it returns; a test per command
  still catches a pattern that is valid but not the one you meant.

## Numeric suffixes

A mnemonic ending in `#` takes an optional number: `CHAN2`, `OUTP3`. The
callback reads it by position along the matched path:

```c
static viper_scpi_result_t cmd_chan_volt(viper_scpi_context_t *ctx)
{
    int32_t ch = viper_scpi_suffix(ctx, 0, 1);   /* 1st '#' mnemonic, default 1 */
    if (ch < 1 || ch > 4) {
        return VIPER_SCPI_RES_ERR_SUFFIX_RANGE;      /* -114 */
    }
    /* ... act on channel ch ... */
    return VIPER_SCPI_RES_OK;
}
```

- Index 0 is the leftmost `#` mnemonic of the full path, including mnemonics
  inherited from an earlier command on the line (see below).
- An omitted suffix, or a skipped optional `#` mnemonic, keeps its index and
  returns the default you pass.
- At most `VIPER_SCPI_MAX_SUFFIXES` (4) are recorded per command; a path with
  more `#` mnemonics still matches, and the later ones read as the default. A
  suffix has at most 9 digits.

## Relative paths and `;`

A program message may hold several commands separated by `;`. As SCPI-99
specifies, a command without a leading `:` is taken relative to the path of
the previous one, minus that command's last mnemonic:

| Message | Second command resolves as |
| --- | --- |
| `SOUR:VOLT 1;CURR 2` | `SOUR:CURR 2` |
| `SOUR:VOLT 1;:OUTP ON` | `OUTP ON` (leading `:` = root) |
| `SOUR:VOLT 1;*OPC;CURR 2` | `SOUR:CURR 2` (common commands do not change the path) |
| `SOUR:VOLT 1;SOUR:CURR 2` | `SOUR:SOUR:CURR 2` -> -113 |

The last row surprises people, but it is the standard. Some host programs
send exactly that; `VIPER_SCPI_OPT_RELATIVE_FALLBACK` makes the parser retry
such a command from the root (see [Configuration](configuration.md)).

Every new program message (each line, in the stream model) starts at the root.

## Common commands

IEEE 488.2 common commands are built in: `*IDN?`, `*RST`, `*CLS`, `*OPC`,
`*OPC?`, `*WAI`, `*STB?`, `*ESR?`, `*ESE`, `*ESE?`, `*SRE`, `*SRE?`, `*TST?`,
and `*TRG` when the configuration has a `trigger` hook.

| Command | What the library does | Your hook |
| --- | --- | --- |
| `*IDN?` | `manufacturer,model,serial,firmware` from the configuration | `idn_*` fields |
| `*RST` | disarms `*OPC`, calls the hook | `cfg.reset`: restore the instrument's reset state |
| `*TRG` | calls the hook | `cfg.trigger`; without it `*TRG` is -113 |
| `*TST?` | answers the hook's result, or 0 (passed) without one | `cfg.self_test` |
| `*CLS` | clears the error queue, ESR and STATus events, then calls the hook | `cfg.clear`: clear your own event registers |
| others | status registers, see [Status and errors](status-and-errors.md) | -- |

The instrument's own common commands (`*SAV`, `*RCL`, `*OPT?`, `*CAL?`, ...)
go in `cfg.common`, a node list whose patterns are the names without `*`:

```c
static const viper_scpi_node_t common[] = {
    { "SAV", cmd_sav, NULL },
    { "RCL", cmd_rcl, NULL },
    { "OPT", cmd_opt, NULL },
    VIPER_SCPI_NODE_END
};
/* cfg: .common = common */
```

It is searched before the built-in table, so an entry there also replaces a
built-in command. Like the built-ins, these commands do not change the path
for relative headers.

## Built-in SCPI commands

Searched after your tree, so your tree can replace any of them:

- `SYSTem:ERRor[:NEXT]?`, `SYSTem:ERRor:COUNt?`, `SYSTem:VERSion?` (answers `1999.0`)
- `STATus:OPERation...`, `STATus:QUEStionable...`, `STATus:PRESet` (see
  [Status and errors](status-and-errors.md))

## Limits

| Limit | Default | Error when exceeded |
| --- | --- | --- |
| Mnemonics in one header (including inherited path) | 8 (`VIPER_SCPI_MAX_RECURSION_DEPTH`) | -112 |
| Parameters per command | 8 (`VIPER_SCPI_MAX_PARAMS`) | -108 |
| Bytes per program message | 128 (`VIPER_SCPI_BUFFER_SIZE`) | -363 |
