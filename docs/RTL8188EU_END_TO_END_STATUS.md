# RTL8188EU Wi-Fi: verified state versus end-to-end support

## What the current driver proves

The driver recognizes the supported USB IDs, configures bulk endpoints, performs vendor register access, attempts to load `/SYSTEM/RTL8188EU.BIN`, checks firmware-download status, and parses received RX aggregates to cache Beacon/Probe Response metadata. The new status API distinguishes USB binding, firmware-load pending, firmware-init-ready, firmware-load failure, and disconnection.

`RTL8188EU_STATUS_FIRMWARE_READY` means only that the device reported its firmware-init-ready bit. It does **not** mean the radio is calibrated, actively scanning, associated, WPA2-authenticated, or able to carry IP traffic. Do not treat firmware readiness as a network link-up signal.

## Work still required for end-to-end Wi-Fi

- RTL8188EU MAC/eFuse and RF/baseband initialization, board-specific calibration, and power tables.
- Channel configuration and active probe-request scanning.
- 802.11 authentication and association request/response state machine.
- Station MAC address sourced from the adapter, not fabricated.
- Secure WPA2-PSK/CCMP supplicant wired to real EAPOL TX/RX and atomic key installation.
- Protected 802.11 data TX/RX, CCMP packet-number replay protection, 802.11 data/LLC-SNAP conversion, and `net_device_t` registration.
- DHCP/DNS/TCP traffic tests and physical adapter validation.

The WPA2/CCMP primitives have standalone tests, but passing primitive tests does not prove association or Internet connectivity. These requirements must be implemented and tested before describing the adapter as end-to-end supported.

## Validation in this change

The RTL8188EU mocked-device test now checks the observable state transitions. This verifies the status contract only; it is not a substitute for association, secured data transfer, or physical hardware testing.