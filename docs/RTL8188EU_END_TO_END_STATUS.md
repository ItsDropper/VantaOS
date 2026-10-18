# RTL8188EU Wi-Fi: verified state versus end-to-end support

## What the current driver proves

The driver recognizes the supported USB IDs, configures bulk endpoints, performs vendor register access, attempts to load `/SYSTEM/RTL8188EU.BIN`, checks firmware-download status, and parses received RX aggregates to cache Beacon/Probe Response metadata. The status API distinguishes USB binding, firmware-load pending, firmware-init-ready, firmware-load failure, and disconnection. It can read the programmed station MAC, but **all TX is now deliberately blocked**: the driver has not completed independently verified MAC/PHY/RF/eFuse initialization, calibration, or channel programming. The previous probe-request helper was only a single-channel transmit primitive and did not prove the radio was operational.

`RTL8188EU_STATUS_FIRMWARE_READY` means only that the device reported its firmware-init-ready bit. It does **not** mean the radio is calibrated, actively scanning, associated, WPA2-authenticated, or able to carry IP traffic. Do not treat firmware readiness as a network link-up signal.

## Work still required for end-to-end Wi-Fi

- RTL8188EU MAC/eFuse and RF/baseband initialization, board-specific calibration, and power tables.
- RF channel selection/hopping and a complete active scan across supported channels. TX remains fail-closed until the radio initialization/calibration stage exists.
- The new bounded 802.11 open-authentication and association frame codecs build requests and parse responses, but no live retry/timeout state machine is wired to the RTL8188EU RX/TX path.
- Station MAC address sourced from the adapter, not fabricated.
- Secure WPA2-PSK/CCMP supplicant wired to real EAPOL TX/RX and atomic key installation.
- Protected 802.11 data TX/RX and `net_device_t` registration. Independent CCMP packet-number allocation, 48-bit header encode/decode, a 64-packet replay window, nonce construction, and bounded Ethernet/LLC-SNAP conversion helpers now exist and have host tests, but are not wired into the hardware data path.
- DHCP/DNS/TCP traffic tests and physical adapter validation.

The WPA2/CCMP primitives have standalone tests, but passing primitive tests does not prove association or Internet connectivity. These requirements must be implemented and tested before describing the adapter as end-to-end supported.

## Validation in this change

The RTL8188EU mocked-device test checks observable state transitions, firmware-download ordering, passive beacon parsing, and that scan/raw TX fail closed while radio initialization is absent. The 802.11 host test exercises Open System Authentication request construction, Authentication Response parsing, Association Request construction with a bounded RSN IE, Association Response/status/AID parsing, and bounded Ethernet/LLC-SNAP conversion, including malformed/truncated inputs. CCMP host tests cover the AES-CCM known-answer vector, tamper handling, packet-number allocation/exhaustion, header/nonce encoding, and out-of-order/replayed/too-old packet numbers. WPA2 tests also cover invalid reconnect cleanup. These tests validate selected components only; they do not establish live association, secured data transfer, or physical hardware functionality.