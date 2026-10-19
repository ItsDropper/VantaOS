# RTL8188EU architecture review follow-up

This note separates silicon limits from driver implementation gaps so follow-up work targets real problems.

## Corrected capability assessment

- **5 GHz is not a driver feature that can be enabled on RTL8188EU.** RTL8188E/RTL8188EU is a 2.4 GHz 802.11b/g/n, single-transmit/single-receive (1T1R) chipset. No register table, rate setting, or software change can make this RF front end operate on 5 GHz. Supporting 5 GHz requires a different dual-band chipset and its own driver.
- **802.11n is a real hardware capability, but HT operation is not yet established by this driver.** The current association helper advertises mandatory legacy 2.4 GHz rates. Do not report HT throughput or MCS negotiation as supported until HT Capabilities/HT Operation handling, AP negotiation, aggregation policy, and real-device tests are implemented.
- **Multiple adapters are not supported by the current RTL8188EU implementation.** Register transport, RX/TX buffers, station state, keys, firmware staging, and network interface are file-static globals. The probe routine rejects a second device while one is bound. This is a deliberate safety boundary; merely changing the guard would cause cross-device state corruption.

## Review items and implementation status

| Review item | Status | Correct next step |
| --- | --- | --- |
| Synchronous firmware polling | Open | Convert firmware download/ready checks into a poll-driven state machine. Each invocation should perform a bounded amount of USB work, preserve stage/offset/deadline state, and fail on timeout or disconnect. A smaller spin count is not an asynchronous fix. |
| Global single-device state | Guarded, not solved | Introduce an explicit per-adapter context, then pass that context through register access, firmware, RX/TX, scan, station, security, and network callbacks. Add two-device interleaving and independent-disconnect tests before enabling a second instance. |
| 5 GHz | Hardware limitation | Do not implement for RTL8188EU. Select a dual-band chipset if 5 GHz is a requirement. |
| Advanced 802.11n | Incomplete driver feature | Add negotiated HT capabilities/operation, one-stream MCS handling, and only then aggregation if the TX/RX descriptor and reorder paths can support it. Test against real APs. |

## Safety and validation policy

The driver must remain fail-closed when PHY initialization is unverified. Firmware-ready is not radio-ready, and host-mocked USB tests cannot validate RF calibration or over-the-air operation. The missing trusted board-specific `RTL8188EU.PHY` profile and physical adapter testing remain independent blockers to end-to-end Wi-Fi.

This review follow-up intentionally does not claim that asynchronous firmware download, multi-adapter operation, HT negotiation, or 5 GHz support has been implemented. The accompanying regression test makes the current single-device rejection behavior explicit so later context refactoring has a stable safety contract.
