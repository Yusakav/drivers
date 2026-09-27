# CH32X035 USB PD Validation

Use a PD protocol analyzer and an independently calibrated VBUS/current meter.

1. Verify attach orientation on CC1 and CC2, debounce, detach and reattach.
2. Negotiate every configured Fixed, Variable and Battery PDO and compare the
   Request RDO, Accept, PS_RDY and measured VBUS.
3. Exercise PPS and SPR AVS limits, quantization and the periodic PPS Request.
4. Enter EPR only after an SPR Explicit Contract. Verify Enter Acknowledged,
   Enter Succeeded, chunked 8-11 PDO capabilities and EPR_Request object index.
5. Test EPR and non-EPR cables. The Source performs cable discovery; verify Enter
   Failed handling for a non-EPR cable.
6. Verify 375 ms Sink KeepAlive, KeepAlive_Ack timeout, renegotiation to 20 V or
   below before Exit, and removal of EPR capabilities while active.
7. Drop GoodCRC packets and confirm the original MessageID is retried twice and
   increments only after a matching GoodCRC.
8. Inject duplicate frames, malformed chunks, Soft Reset, Hard Reset and RX queue
   overflow. Confirm recovery and that Soft Reset preserves the Explicit Contract.
9. Send 25-byte Source Capabilities Extended, 24-byte Sink Capabilities Extended,
   Status, PPS Status, Revision and Source Info messages and verify DPM payloads.
10. Exercise Security, Firmware Update and Vendor Defined Extended payloads in
    both directions through the DPM callback and public raw send APIs.
11. Drain `usbpd_observer_service()` under traffic and verify message names,
    MessageID, chunk metadata, PDO/RDO summaries, raw bytes and dropped count.
12. In Source mode, verify the advertised Rp level matches the 5 V PDO, then
    negotiate Fixed, Variable, Battery, PPS and SPR AVS contracts. Confirm the
    board callback reaches the requested voltage before PS_RDY.
13. In DRP mode, verify 75 ms Rd/Rp toggling, preferred-role startup, Sink and
    Source attachment, detach recovery, and return to toggling.
14. Request Source Capabilities Extended, Sink Capabilities, Sink Capabilities
    Extended, Source Info, Status, PPS Status and Revision while in Source mode.
15. Trigger a received Hard Reset while sourcing and confirm VBUS is disabled,
    discharge is enabled, and no second Hard Reset is transmitted in response.

Source and DRP operation require board-specific `set_source`, `source_ready`,
and preferably `set_discharge` callbacks. Do not advertise EPR Source, VCONN
Source, PR_Swap or FRS capabilities: those policy paths are rejected until the
required cable discovery, power-path and fault controls are implemented.
