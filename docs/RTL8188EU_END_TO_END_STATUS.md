# RTL8188EU Wi-Fi: verified state versus end-to-end support

## What the current driver proves

The driver recognizes the supported USB IDs, configures bulk endpoints, performs vendor register access, attempts to load `/SYSTEM/RTL8188EU.BIN`, checks firmware-download status, and parses received RX aggregates to cache Beacon/Probe Response metadata. The status API distinguishes USB binding, firmware-load pending, firmware-init-ready, firmware-load failure, and disconnection. The driver also reads the programmed station MAC and can transmit a wildcard Probe Request on the radio's current channel; this is a single-channel primitive, not a channel-hopping scan.

`RTL8188EU_STATUS_FIRMWARE_READY` means only that the device reported its firmware-init-ready bit. It does **not** mean the radio is calibrated, actively scanning, associated, WPA2-authenticated, or able to carry IP traffic. Do not treat firmware readiness as a network link-up signal.

## Work still required for end-to-end Wi-Fi

- RTL8188EU MAC/eFuse and RF/baseband initialization, board-specific calibration, and power tables.
- RF channel selection/hopping and a complete active scan across supported channels. The new probe-request API sends only on the currently configured channel.
- The new bounded 802.11 open-authentication and association frame codecs build requests and parse responses, but no live retry/timeout state machine is wired to the RTL8188EU RX/TX path.
- Station MAC address sourced from the adapter, not fabricated.
- Secure WPA2-PSK/CCMP supplicant wired to real EAPOL TX/RX and atomic key installation.
- Protected 802.11 data TX/RX, CCMP packet-number replay protection, 802.11 data/LLC-SNAP conversion, and `net_device_t` registration.
- DHCP/DNS/TCP traffic tests and physical adapter validation.

The WPA2/CCMP primitives have standalone tests, but passing primitive tests does not prove association or Internet connectivity. These requirements must be implemented and tested before describing the adapter as end-to-end supported.

## Validation in this change

The RTL8188EU mocked-device test checks observable state transitions and validates the active Probe Request's destination, station address, BSSID, and wildcard SSID/rates IEs. The 802.11 host test also exercises Open System Authentication request construction, Authentication Response parsing, Association Request construction with a bounded RSN IE, and Association Response/status/AID parsing, including malformed/truncated responses. This validates selected frame codecs and the low-level status/TX contract only; it is not a substitute for live association, secured data transfer, or physical hardware testing.