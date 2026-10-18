# RTL8188EU Wi-Fi: verified state versus end-to-end support

## What is now implemented

The driver binds the supported USB IDs, configures bulk endpoints, loads the externally supplied `/SYSTEM/RTL8188EU.BIN` firmware, parses RX aggregates, and caches passive Beacon/Probe Response metadata. Firmware offload now initializes the packet-buffer LLT, reads the physical eFuse map through the packet-buffer debug interface, decodes compressed/extended logical sections with bounds checks, validates required factory identity/calibration fields, and applies the firmware eFuse patch. The runtime MAC must be valid and match the decoded factory MAC.

The control/data path now has these implementations:
- RF path-A serial register read/write helpers and RF channel-register programming with readback verification for 2.4 GHz channels 1-14.
- Firmware-offloaded LLT initialization and eFuse-map acquisition, physical-to-logical section decoding, factory MAC/crystal/thermal/board-field validation, and a host regression test for extended eFuse sections.
- A channel-hopping scan state machine with probe requests and bounded per-channel poll dwell.
- Live Open System Authentication and Association Request/Response state handling, including response status checks, AID validation, retry limits, and poll-driven timeouts.
- WPA2-PSK/CCMP EAPOL message handling connected to received 802.11 data frames and the real USB TX path. The kernel CSPRNG is required; no weak fallback is used.
- CCMP protected unicast/multicast data framing, PN allocation and replay-window checks, bounded LLC/SNAP conversion, and a registered `net_device_t` path when the network core is present.
- Nonblocking DHCP acquisition started after WPA2 authorization, plus credential/key cleanup on connection failure or USB disconnect.

## Critical limitation: radio initialization is still incomplete

**This driver still cannot connect to a real access point yet.** The radio-ready flag is intentionally never enabled. RF serial access and channel programming are implemented, but they are not a substitute for the required RTL8188EU power/MAC/baseband initialization, eFuse-derived board configuration, TX-power setup, and LC/IQ calibration. Without those stages, probe/auth/association transmissions remain fail-closed.

Realtek's public product page identifies the RTL8188EUS as a 2.4 GHz 802.11b/g/n 1T1R USB chipset, but it does not provide the register-level initialization and calibration data needed to complete this independently. I have not copied third-party driver code, register tables, calibration routines, or firmware blobs into VantaOS.

## Still required before end-to-end support can be claimed

- Independently sourced, revision-correct MAC/PHY/RF initialization and eFuse/board calibration.
- Enable radio TX only after successful initialization and calibration, with stage-specific error reporting.
- Verify actual channel tuning, scan results, auth/association, WPA2 handshake, CCMP data transfer, and DHCP/DNS against a physical adapter and WPA2-PSK 2.4 GHz AP.
- Add tests for the new station/data path and validate descriptor/FCS semantics against real hardware. Host tests cannot prove RF behavior.


## Validation in this change

The RTL8188EU mocked-device test checks observable state transitions, firmware-download ordering, passive beacon parsing, and that scan/raw TX fail closed while radio initialization is absent. The 802.11 host test exercises Open System Authentication request construction, Authentication Response parsing, Association Request construction with a bounded RSN IE, Association Response/status/AID parsing, and bounded Ethernet/LLC-SNAP conversion, including malformed/truncated inputs. CCMP host tests cover the AES-CCM known-answer vector, tamper handling, packet-number allocation/exhaustion, header/nonce encoding, and out-of-order/replayed/too-old packet numbers. WPA2 tests also cover invalid reconnect cleanup. These tests validate selected components only; they do not establish live association, secured data transfer, or physical hardware functionality.