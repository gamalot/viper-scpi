# Large data

[Documentation home](README.md)

Waveforms, screenshots, logs, files and firmware images are larger than any
buffer a small microcontroller can spare. viper-scpi moves them as IEEE 488.2
definite-length blocks (`#<n><length><bytes>`) in both directions without
holding them in RAM.

## Sending: generated block responses

`viper_scpi_result_block_fill()` writes the block header immediately and asks a
producer function for the bytes later, a piece at a time:

```c
/* Bytes [offset, offset + count) of the block, written to dst. */
static void trace_fill(void *user, uint8_t *dst, uint32_t offset, size_t count)
{
    const struct trace *t = user;
    flash_read(t->address + offset, dst, count);
}

static viper_scpi_result_t cmd_trace_data(viper_scpi_context_t *ctx)   /* TRAC:DATA? */
{
    static struct trace trace;
    trace_locate(&trace);
    viper_scpi_result_block_fill(ctx, trace.length, trace_fill, &trace);
    return VIPER_SCPI_RES_OK;
}
```

- **Message-exchange model:** the block is generated while the transport reads
  it, so a reply can be megabytes long from a small `output_size`. Text before
  and after the block is kept in order (`DATA?;*OPC?` gives `#41000...;1\n`).
  One generated block per reply; a second is -400.
- **Stream model:** the block is generated and written immediately in 64-byte
  chunks.
- `fill` may be called after the callback has returned, so `user` must point
  to storage that lives long enough (static, as above).
- The length must be at most 999,999,999 bytes (nine header digits); larger is
  -222.

For data that is already in RAM, `viper_scpi_result_block(ctx, buf, len)` copies
it into the response like any other value.

## Receiving: streaming block input

A command whose block parameter can be larger than the line buffer opts in
with a table in the configuration:

```c
static const viper_scpi_node_t mmem_nodes[] = {
    { "DATA", cmd_mmem_data, NULL },   /* still handles MMEM:DATA? and small #0 blocks */
    VIPER_SCPI_NODE_END
};

static viper_scpi_result_t mmem_data_stream(viper_scpi_context_t *ctx,
                                            viper_scpi_stream_event_t ev,
                                            const uint8_t *data, size_t len)
{
    static char name[32];

    switch (ev) {
    case VIPER_SCPI_STREAM_BEGIN:       /* MMEM:DATA 'name',#<n><len> -- len is the size */
        if (viper_scpi_param_string(ctx, 0, name, sizeof name, NULL) != VIPER_SCPI_RES_OK) {
            return VIPER_SCPI_RES_ERR_DATA_TYPE;
        }
        return file_open(name, len) ? VIPER_SCPI_RES_OK : VIPER_SCPI_RES_ERR_EXECUTION;
    case VIPER_SCPI_STREAM_DATA:        /* the next len bytes, as they arrive */
        return file_write(data, len) ? VIPER_SCPI_RES_OK : VIPER_SCPI_RES_ERR_OUT_OF_MEMORY;
    case VIPER_SCPI_STREAM_END:         /* every byte received */
        file_close();
        return VIPER_SCPI_RES_OK;
    case VIPER_SCPI_STREAM_ABORT:       /* cut short: throw the partial file away */
    default:
        file_discard();
        return VIPER_SCPI_RES_OK;
    }
}

static const viper_scpi_stream_node_t streams[] = {
    { &mmem_nodes[0], mmem_data_stream },
    { NULL, NULL }
};
/* cfg: .stream_nodes = streams */
```

How it works: as soon as the block's length digits have arrived, the library
resolves the command -- header and the parameters before the block -- and from
then on passes the block's bytes to the handler instead of the line buffer.
`viper_scpi_input_buffer()` hands over whole runs, so DATA chunks are as large
as the transport's packets. The handler runs in the context that feeds input,
so it should not block for long (queue the flash writes if they are slow).

Rules:

| Situation | Result |
| --- | --- |
| Block of the **first** command of a message, definite length, node in the table | streamed |
| Anywhere else, an indefinite `#0` block, or a node not in the table | the ordinary line buffer (too large: -363) |
| Message ends before the declared length | ABORT, then -161 |
| `viper_scpi_device_clear()` or `viper_scpi_input_discard()` mid-block | ABORT, no error |
| A parameter after the block (`#14abcd,5`) | ABORT, -108, the rest of that command skipped |
| BEGIN or DATA returns an error | queued; the rest of the block is skipped without further calls |
| Commands after the streamed one (`MMEM:DATA ...;*OPC?`) | run normally, starting from the root path |
| END writes results | allowed; readable once the whole message is done |

The parameters before the block (`'name'` above) can be read during BEGIN,
DATA and END.

## Choosing a design

| Need | Use |
| --- | --- |
| A response of a few hundred bytes | ordinary `result_*`; size `output_size` for it |
| A response larger than RAM | `viper_scpi_result_block_fill()` |
| An upload that fits in the line buffer | `viper_scpi_param_block()` |
| An upload larger than the line buffer | streaming block input |
| Many small numbers (`TRAC:DATA 1,2,3,...`) | raise `VIPER_SCPI_MAX_PARAMS` and `VIPER_SCPI_BUFFER_SIZE`, or switch the protocol to a block |
