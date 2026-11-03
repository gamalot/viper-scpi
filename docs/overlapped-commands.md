# Overlapped commands

[Documentation home](README.md)

Some commands start something that takes time: a measurement, a relay that
must settle, a sweep. IEEE 488.2 calls these *overlapped* commands and gives
the controller three ways to wait for them: `*OPC`, `*OPC?` and `*WAI`.
viper-scpi implements all three without ever blocking the main loop.

## Marking operations

A callback that starts a slow operation calls `viper_scpi_op_begin()` and
returns at once. When the operation finishes, the application calls
`viper_scpi_op_end()`:

```c
static viper_scpi_result_t cmd_init(viper_scpi_context_t *ctx)   /* INITiate */
{
    adc_start_conversion();
    viper_scpi_op_begin(ctx);
    return VIPER_SCPI_RES_OK;
}

void main_loop_step(void)
{
    if (adc_done_flag) {                 /* set by the ADC interrupt */
        adc_done_flag = false;
        viper_scpi_op_end(&scpi);
    }
    viper_scpi_poll(&scpi);              /* resumes suspended messages */
}
```

Operations are counted: two `op_begin()` need two `op_end()`. Call these from
the same context as the other library functions -- an interrupt sets a flag,
the main loop calls `op_end()`.

## What the controller sees

| Command | No operation pending | Operations pending |
| --- | --- | --- |
| `*OPC` | sets OPC in the ESR now | sets OPC when the last one ends (can raise SRQ) |
| `*OPC?` | answers `1` now | the message is suspended; `1` is answered on completion |
| `*WAI` | nothing | the rest of the message is suspended until completion |

Three common controller patterns:

```
INIT;*WAI;FETCH?        -> FETCH? runs only after the measurement; one reply
INIT;*OPC?              -> the reply "1" arrives when done (read with a long timeout)
*ESE 1;*SRE 32;INIT;*OPC -> no read at all; SRQ when done, then FETCH?
```

## Suspension never blocks

When `*WAI` or a pending `*OPC?` suspends a message, the commands after it stay
in the line buffer. `viper_scpi_poll()` resumes them once no operation is
pending; it does nothing otherwise, so call it on every pass of the main loop.
`op_end()` itself never runs commands.

While a message is suspended:

- `viper_scpi_busy()` is true and **input is refused**. The transport should
  hold the controller off: USBTMC NAKs bulk-OUT, a UART deasserts RTS or
  buffers, or keeps them in its receive ring. Bytes fed anyway are refused:
  the message they belong to is dropped whole -- also if the suspension ends
  before its last byte -- and reported as -363 at its end. It is never
  executed, so it does not interrupt (-410) the reply of the suspended
  message either.
  `viper_scpi_input_until_busy()` feeds a chunk only up to the message that
  suspends and returns how many bytes it took, so the rest can be fed later.
- In the message-exchange model the response is not readable yet (MAV clear;
  `viper_scpi_output_read()` returns nothing), because the response message is
  not complete. In the stream model, responses produced before the suspension
  have already been written.

## Cancelling

- `viper_scpi_device_clear()` cancels a suspended message, an armed `*OPC` and
  a waiting `*OPC?` -- IEEE 488.2's Operation Complete Command and Query Idle
  States.
- `*CLS` and `*RST` disarm `*OPC`.
- The operations themselves belong to the application: device clear does not
  stop the ADC. If a clear should abort them, do that in your clear handler and
  call `op_end()` for each.

## Example: a meter

```c
static float g_reading;

static viper_scpi_result_t cmd_init(viper_scpi_context_t *ctx)
{
    if (viper_scpi_op_pending(ctx)) {
        return VIPER_SCPI_RES_ERR_EXECUTION;          /* -200: already measuring */
    }
    viper_scpi_status_condition(ctx, VIPER_SCPI_STATUS_OPER, 1u << 4, 1u << 4); /* MEASuring */
    adc_start_conversion();
    viper_scpi_op_begin(ctx);
    return VIPER_SCPI_RES_OK;
}

static viper_scpi_result_t cmd_fetch(viper_scpi_context_t *ctx)   /* FETCh? */
{
    viper_scpi_result_double_digits(ctx, g_reading, 7);
    return VIPER_SCPI_RES_OK;
}

void on_adc_done(float value)    /* from the main loop */
{
    g_reading = value;
    viper_scpi_status_condition(&scpi, VIPER_SCPI_STATUS_OPER, 1u << 4, 0);
    viper_scpi_op_end(&scpi);
}
```

A controller then gets the effect of `READ?` by sending `INIT;*WAI;FETC?` as
one message: `FETC?` runs only once the reading exists.

## Slow queries: `viper_scpi_defer()`

When the host just sends `READ?` (or `MEAS:CURR?`) and the answer takes time,
the query itself can wait without blocking the main loop:

```c
static viper_scpi_result_t cmd_read(viper_scpi_context_t *ctx)   /* READ? */
{
    if (!viper_scpi_resumed(ctx)) {
        adc_start_conversion();       /* on_adc_done() calls op_end() */
        viper_scpi_op_begin(ctx);
        viper_scpi_defer(ctx);        /* finish me later */
        return VIPER_SCPI_RES_OK;
    }
    viper_scpi_result_double_digits(ctx, g_reading, 7);
    return VIPER_SCPI_RES_OK;
}
```

The message is suspended as at `*WAI` (`viper_scpi_busy()` is true, the reply
is not readable yet). Once no operation is pending, `viper_scpi_poll()` calls
the callback again with the same parameters and suffixes and
`viper_scpi_resumed()` true; that call writes the result, and the rest of the
message runs. `viper_scpi_device_clear()` cancels it like any suspension.
Only a command callback can defer: in a stream handler (see
[Receiving: streaming block input](large-data.md#receiving-streaming-block-input)) `viper_scpi_defer()` does nothing.
