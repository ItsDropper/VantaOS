# RTL8188EU real-hardware acceptance testing

## Purpose

The host-side `scripts/test_rtl8188eu.c` test uses mocked xHCI transfers and synthetic beacon frames. It is useful for parser/driver regressions, but it **cannot prove that radio transmission, association, WPA2, DHCP, DNS, or Internet access works**.

For that reason, `scripts/test_rtl8188eu_hardware.py` is a separate, fail-closed verifier for serial output from a physical RTL8188EU adapter. It requires evidence for every stage below and never converts missing evidence into a pass.

## Required setup

- A physical USB adapter verified to contain an RTL8188EU/RTL8188E chipset.
- The exact adapter's trusted firmware file at `/SYSTEM/RTL8188EU.BIN`.
- A PHY profile validated for that board and silicon cut at `/SYSTEM/RTL8188EU.PHY`. Do not use guessed register values or another board's calibration.
- A 2.4 GHz WPA2-PSK access point and test credentials. Do not commit credentials or put them in serial logs.
- A serial console capture from the actual VantaOS boot and test session.
- A real on-device DNS/TCP/HTTP probe that emits the corresponding success markers only after validating received responses.

## Run the verifier

Save the complete serial capture as `rtl8188eu-serial.log`, then run:

```sh
python3 scripts/test_rtl8188eu_hardware.py rtl8188eu-serial.log
```

Exit code 0 means every required marker was present and no known driver failure marker was logged. Exit code 1 means a stage failed or is unverified. Exit code 2 means the log could not be read.

Required evidence, in order:

1. USB control/endpoint transport is working.
2. Firmware upload and MCU-ready check succeeded.
3. eFuse identity read succeeded.
4. The board-specific PHY profile and calibration sequence completed.
5. The driver enabled the radio.
6. Active scanning completed and received at least one real network beacon/probe response.
7. The AP accepted Open System authentication and association.
8. WPA2 four-way handshake completed and CCMP keys were installed.
9. DHCP supplied a configured IPv4 lease.
10. A real DNS lookup succeeded using the configured DNS server.
11. A real TCP connection exchanged and validated application data.
12. An HTTP response was received and validated over that connection.

The last three stages require these explicit markers, which must only be emitted by a real on-device test after checking the response:
- `VANTAOS_RTL8188EU_REAL_DNS_PROBE_OK`
- `VANTAOS_RTL8188EU_REAL_TCP_PROBE_OK`
- `VANTAOS_RTL8188EU_REAL_HTTP_PROBE_OK`

## Current limitations (do not treat as a pass)

This PR adds milestone logging and the acceptance-log gate; it does **not** itself run a physical adapter. The current driver still lacks a bundled, verified board-specific PHY profile. The OS also does not yet expose a complete operator-triggerable workflow that connects to an AP and runs DNS/TCP/HTTP probes, so the real-hardware verifier is expected to report missing stages until that workflow exists and has been run.

The Python verifier's unit tests use synthetic log strings to test the verifier only. They are not hardware tests. CI success means the code and verifier tests pass; it is not evidence of RF or Internet functionality.

## What counts as proof

Do not mark Wi-Fi end-to-end complete based only on:
- USB device detection;
- firmware-ready or PHY-profile parsing;
- a synthetic beacon in a mocked test;
- a transmitted probe request without a real response;
- successful WPA2 unit tests using fake callbacks;
- DHCP client unit tests using fabricated packets.

End-to-end acceptance requires the physical-hardware serial capture, all 12 milestones, and independently verified successful responses at DNS, TCP, and HTTP layers.
