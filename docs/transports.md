# Transports

[Documentation home](README.md)

viper-scpi does not talk to hardware. The transport driver feeds it bytes and
tells it where messages end; the library hands back responses. Which output
model to use depends on who decides when a response is sent.

| | Stream model | Message-exchange model |
| --- | --- | --- |
| Transports | UART, USB CDC, raw TCP (port 5025), Telnet | USBTMC/USB488, VXI-11, HiSLIP, GPIB device |
| Who sends the reply | the device, as soon as it exists | the controller requests it |
| Message end | `\n` | EOM / END flag (and `\n`) |
| Configure | `.write = fn` | `.output_buffer = buf, .output_size = n` |
| MAV in the status byte | always 0 | set while a reply waits |
| -410 / -420 | not applicable | apply |

## Stream model

Set `write`; leave `output_buffer` NULL. Every response is passed to `write`
as it is produced, in one or more pieces, ending with `\n`.

```c
static size_t tcp_write(void *user, const char *data, size_t len)
{
    struct conn *c = user;              /* cfg.user_data */
    size_t done = 0;

    while (done < len) {                /* blocking socket: send it all */
        long k = net_send(c->sock, data + done, len - done);
        if (k <= 0) {
            break;                      /* connection lost */
        }
        done += (size_t)k;
    }
    return done;
}
```

`write` must take all the bytes it is given (block or buffer). If it returns
less, the library writes nothing more of that response message, terminator
included, and queues -321, so the host times out instead of reading a cut-off
value as valid.

Input: call `viper_scpi_input_buffer()` with whatever the driver received; a
`\n` executes the message, and `\r` is white space, so `\r\n` works too. If
one segment can hold several commands and one of them suspends execution
(`INIT;*WAI;FETC?`), use `viper_scpi_input_until_busy()` instead: it returns
how many bytes it consumed, and the rest is fed once `viper_scpi_busy()` is
false again (`viper_scpi_input_buffer()` would refuse it with -363). In a byte stream LF always ends
the message, even inside a quoted string, so one lost quote cannot swallow
every following command.

If the link has its own framing (a USB CDC packet that is always one command
without a newline), call `viper_scpi_end_of_message()` after the data.

## Message-exchange model

On these transports the controller writes a message, then asks for the reply,
possibly in pieces, and IEEE 488.2 attaches rules to the waiting reply. Give
the configuration an output buffer:

```c
static char reply[512];      /* the largest response message, except generated blocks */

static const viper_scpi_config_t cfg = {
    .root = root,
    .output_buffer = reply,
    .output_size = sizeof reply,
    .reset = on_reset,
    .trigger = on_trigger,   /* enables *TRG (USB488 TRIGGER message, GPIB GET) */
    .idn_manufacturer = "Acme", .idn_model = "DMM-1", .idn_serial = "7", .idn_firmware = "2.0",
};
```

### Event map

| Transport event | Call |
| --- | --- |
| bytes of a message arrive | `viper_scpi_input_buffer(ctx, data, n)` |
| last packet of the message (USBTMC EOM, VXI-11 END, HiSLIP DataEnd) | `viper_scpi_end_of_message(ctx)` |
| controller requests up to *n* bytes (USBTMC REQUEST_DEV_DEP_MSG_IN, VXI-11 device_read) | `k = viper_scpi_output_read(ctx, buf, n, &end)`; send `k` bytes, with EOM/END set when `end` is true |
| request while nothing is queued | if `viper_scpi_busy(ctx)`, the reply is still being produced (`INIT;*OPC?`): keep the request and answer it from the main loop once `viper_scpi_output_pending(ctx)`. Otherwise `viper_scpi_error_push(ctx, VIPER_SCPI_RES_ERR_QUERY_UNTERMINATED)` (-420), then let the request time out or send an empty transfer, as the transport specifies |
| controller aborts the reply (USBTMC INITIATE_ABORT_BULK_IN) | `viper_scpi_output_discard(ctx)` |
| controller aborts incoming data (USBTMC INITIATE_ABORT_BULK_OUT, CLEAR_FEATURE on bulk OUT, VXI-11 abort) | `viper_scpi_input_discard(ctx)`: the queued reply stays readable |
| device clear (USBTMC INITIATE_CLEAR, VXI-11 device_clear, GPIB DCL/SDC, `viClear`) | `viper_scpi_device_clear(ctx)` |
| serial poll (USB488 READ_STATUS_BYTE, VXI-11 device_readstb, GPIB serial poll) | `viper_scpi_serial_poll(ctx)` |
| main loop | `viper_scpi_poll(ctx)`; `if (viper_scpi_take_service_request(ctx))` raise SRQ |
| before accepting more input | hold off while `viper_scpi_busy(ctx)` (overlapped commands) |

### Rules the library enforces

- **MAV** (status byte bit 4) is set while a reply is waiting and clears when
  the last byte is read.
- **-410 Query INTERRUPTED.** A new program message arriving over an unread
  or partly read reply discards that reply (IEEE 488.2 6.3.2.3). An empty
  message is not a message.
- **-321 Out of memory.** A reply that does not fit `output_size` is dropped
  whole; a truncated reply would look like a valid one.
- **Strings may contain line feeds.** The message end is signalled out of band,
  so LF and CR inside an open quote are string data (IEEE 488.2 7.7.5).
- **Hidden until complete.** While a message is still executing -- suspended by
  `*WAI`, or still receiving a streamed block -- its partial reply cannot be
  read and MAV stays clear.

### SRQ and serial poll

`viper_scpi_take_service_request()` returns true once for each new service
request (each rising edge of STB & SRE, tracked as it happens, so
`*CLS;*SRE 32;*OPC` on one line is not missed). Answer it with the
transport's SRQ mechanism. The context then latches RQS:

```c
if (viper_scpi_take_service_request(&scpi)) {
    uint8_t stb = viper_scpi_serial_poll(&scpi);   /* RQS in bit 6, cleared by the poll */
    usb488_send_srq_notification(stb);              /* interrupt-IN carries the STB */
}
```

On USB488 the interrupt-IN notification carries the status byte, and VISA hands
that byte to the next `viReadSTB()` without asking the device -- so sending the
notification *is* the serial poll. Use `viper_scpi_serial_poll()` to produce
it (as above), or `viper_scpi_rqs_delivered()` if you built the byte yourself.
On GPIB and VXI-11, where the controller polls separately, call
`viper_scpi_serial_poll()` when the poll arrives. `*STB?` always reports MSS
and never clears RQS.

### A USBTMC skeleton

The shape of a USB488 instrument on top of a USB stack that offers
TinyUSB-style callbacks. Names of the USB side are illustrative.

```c
static viper_scpi_context_t scpi;
static uint8_t in_buf[64];

/* Bulk-OUT data of a DEV_DEP_MSG_OUT transfer. */
bool usbtmc_msg_data_cb(const void *data, size_t len, bool eom)
{
    if (viper_scpi_busy(&scpi)) {
        return false;                       /* NAK: the stack retries later */
    }
    viper_scpi_input_buffer(&scpi, data, len);
    if (eom) {
        viper_scpi_end_of_message(&scpi);
    }
    return true;
}

static size_t s_read_max;                   /* request waiting for a reply */

/* Answers an outstanding read once a complete reply is queued. */
static void serve_read(void)
{
    bool end = false;
    size_t n;

    if (s_read_max == 0 || !viper_scpi_output_pending(&scpi)) {
        return;
    }
    n = viper_scpi_output_read(&scpi, in_buf, s_read_max, &end);
    s_read_max = 0;
    usbtmc_transmit_dev_msg_data(in_buf, n, end /* EOM */);
}

/* REQUEST_DEV_DEP_MSG_IN: the host wants up to max bytes. */
void usbtmc_msg_in_request_cb(size_t max)
{
    if (!viper_scpi_output_pending(&scpi) && !viper_scpi_busy(&scpi)) {
        viper_scpi_error_push(&scpi, VIPER_SCPI_RES_ERR_QUERY_UNTERMINATED);
        return;                             /* no reply is coming */
    }
    /* Either queued, or still being produced by a message suspended in
     * *OPC? / *WAI: then the main loop answers it after viper_scpi_poll(). */
    s_read_max = (max < sizeof in_buf) ? max : sizeof in_buf;
    serve_read();
}

void usbtmc_initiate_clear_cb(void)        { s_read_max = 0; viper_scpi_device_clear(&scpi); }
void usbtmc_abort_bulk_in_cb(void)         { s_read_max = 0; viper_scpi_output_discard(&scpi); }
void usbtmc_abort_bulk_out_cb(void)        { viper_scpi_input_discard(&scpi); }
uint8_t usb488_read_status_byte_cb(void)   { return viper_scpi_serial_poll(&scpi); }

void main_loop_step(void)
{
    viper_scpi_poll(&scpi);
    serve_read();                           /* a reply that just completed */
    if (viper_scpi_take_service_request(&scpi)) {
        usb488_notify_srq(viper_scpi_serial_poll(&scpi));
    }
}
```

The USB callbacks run in the stack's task context; if yours run in an
interrupt, queue the events and make these calls from the main loop.

## Host software that is not quite standard

Two options in `viper_scpi_config_t.options`, both off by default:

- `VIPER_SCPI_OPT_EOM_MESSAGES` (message-exchange model): LF separates commands
  like `;` and only EOM ends the program message. Use it if a host sends
  `*IDN?\n*OPC?\n` in one transfer and expects both answers; by the standard
  the second query interrupts the first (-410).
- `VIPER_SCPI_OPT_RELATIVE_FALLBACK`: see [Command tree](command-tree.md).
