# USB PD R3.2 Driver

The public API is declared in `inc/usbpd.h`. The CH32X035 backend supports one
port (`port == 0`) configured as Sink, Source, or DRP. Protocol constants and
wire objects are defined in `inc/usbpd_def.h` from USB PD R3.2 V1.1.

## Layers

- `platform/ch32x035/usbpd_phy_ch32x035.*`: registers, CC, Rp/Rd, DMA, IRQ,
  VBUS measurement, transmit, receive, and ISR GoodCRC response.
- `usbpd_protocol.*`: MessageID, GoodCRC validation, retries, duplicate
  filtering, resets, and chunked Extended Messages.
- `pd_sink.*`: Sink attach, SPR, PPS, SPR AVS, EPR/AVS, KeepAlive, and recovery.
- `pd_source.*`: Source attach, SPR PDO evaluation, PPS/SPR AVS timeout, power
  transition callbacks, and capability responses.
- `usbpd.*`: fixed-role dispatch and DRP Rd/Rp toggling.
- `common/message_buffer/`: hardware-independent fixed-size record ring.
- `usbpd_message.*`: task-context observation and readable formatting.
- `usbpd_dpm.h`: application policy, optional payload, and board power callbacks.

## Sink configuration

```c
usbpd_config_t config = {0};

config.port_type = USBPD_PORT_SINK;
config.sink.max_voltage_mv = 5000U;
config.sink.max_current_ma = 1000U;
config.sink.max_power_mw = 5000U;
config.sink.sink_pdo_count = 1U;
config.sink.sink_pdo[0].sink_fixed.current_10ma = 100U;
config.sink.sink_pdo[0].sink_fixed.voltage_50mv = 100U;
config.sink.sink_pdo[0].sink_fixed.type = USBPD_PDO_FIXED;

usbpd_init(0U, &config);
usbpd_task(0U, now_ms);
```

An EPR Sink list has 8 to 11 positions. Positions 1 through 7 contain the SPR
PDOs and any required zero padding; EPR PDOs begin at position 8. EPR entry is
attempted only after an SPR Explicit Contract and uses Extended Control for
EPR capability requests and KeepAlive.

## Source and DRP configuration

A Source must provide at least a 5V Fixed Source PDO and a `dpm.set_source`
callback. `dpm.source_ready` is optional and lets the policy wait for the board
regulator before PS_RDY. `set_discharge` is used during reset recovery.

For DRP, configure both `sink` and `source`, set `port_type` to
`USBPD_PORT_DRP`, and select `preferred_role`. The port alternates Rd and Rp
until attach, then starts the matching policy. Sink PDOs are reused for
Get_Sink_Cap responses while operating as Source.

The Source policy supports Fixed, Variable, Battery, PPS, and SPR AVS
contracts. EPR Source, PR_Swap, VCONN_Swap, and FRS are intentionally not
advertised or accepted. They require additional cable discovery, VCONN, VBUS
switching, discharge, and fault-control integration.

## Optional protocols

Authentication, Firmware Update, USB4, and vendor payload semantics belong to
the application DPM. `message_received` receives reassembled messages in task
context. `get_extended` supplies Status, PPS Status, and Country Codes data.
Responses can also use `usbpd_send_control()`, `usbpd_send_data_objects()`, and
`usbpd_send_extended()`.

## Observation

`usbpd_observer_pop()` returns structured records. `usbpd_observer_service()`
formats direction, SOP, message name, MessageID, PDO/RDO summaries, chunk
metadata, errors, and raw bytes. The 32-entry ring overwrites the oldest record
and reports the drop count. No printing occurs in interrupt context.

Call `usbpd_task()` at least once per millisecond with a monotonic timestamp.
