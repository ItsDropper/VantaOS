# RTL8188EU Wi-Fi: verified state versus end-to-end support

## What is now implemented

The driver binds the supported USB IDs, configures bulk endpoints, loads the externally supplied `/SYSTEM/RTL8188EU.BIN` firmware, parses RX aggregates, and caches passive Beacon/Probe Response metadata. The current branch contains a firmware-offload/packet-buffer path for eFuse access using the TX packet-buffer selector, the saved TX boundary as the debug-port base (8-byte units), and a cleared/reissued IOL mailbox command, plus a bounds-checked physical-to-logical eFuse decoder and factory-field validation. **The hardware transport and offload sequence are not hardware-validated**, so this is not evidence that the physical eFuse map is being read correctly on a real adapter. The host regression test covers the logical-map decoder only. The packet-buffer transport itself remains untested against real hardware. The runtime MAC check is gated on that path succeeding.

The control/data path now has these implementations:
- RF path-A serial register read/write helpers and channel-register readback code for the default 2.4 GHz channels 1-13. The code is gated and not hardware-validated.
- Firmware-offloaded LLT initialization and eFuse-map acquisition, physical-to-logical section decoding, factory MAC/crystal/thermal/board-field validation, and a host regression test for extended eFuse sections.
- A channel-hopping scan state machine with probe requests and bounded per-channel poll dwell.
- Live Open System Authentication and Association Request/Response state handling, including response status checks, AID validation, retry limits, and poll-driven timeouts.
- WPA2-PSK/CCMP EAPOL message handling connected to received 802.11 data frames and the real USB TX path. The kernel CSPRNG is required; no weak fallback is used.
- CCMP protected unicast/multicast data framing, PN allocation and replay-window checks, bounded LLC/SNAP conversion, and a registered `net_device_t` path when the network core is present.
- Nonblocking DHCP acquisition started after WPA2 authorization, plus credential/key cleanup on connection failure or USB disconnect.

## Critical limitation: radio initialization is still incomplete

**This driver still cannot connect to a real access point yet.** The radio-ready flag is intentionally never enabled. RF serial access and channel programming are implemented, but they are not a substitute for the required RTL8188EU power/MAC/baseband initialization, eFuse-derived board configuration, TX-power setup, and LC/IQ calibration. Without those stages, probe/auth/association transmissions remain fail-closed.

## Research findings and constraints

Realtek's public product page confirms the RTL8188EUS is a 2.4 GHz 802.11b/g/n 1T1R USB chipset, but exposes no register programming manual. Public FCC exhibits for an RTL8188EUS module contain RF compliance reports and product photographs, not a usable MAC/BB/RF initialization specification. I could not locate a publicly accessible official Realtek register-level programming guide or eFuse specification that independently defines the required initialization values.

As behavioral research only, the public Linux RTL8188E driver indicates that initialization is not just setting RF channel register 0x18: the flow separately configures MAC registers, enables BB/RF after power sequencing, applies the 1T baseband table, applies eFuse-derived power-group data when eFuse autoload succeeds, applies the AGC table, programs crystal trim, configures RF path A, computes per-channel TX power, and performs LC/IQ calibration. I have not copied Linux driver code, register tables, calibration routines, or firmware blobs into VantaOS. The current branch must not be described as having completed the eFuse hardware path until verified against a real adapter.

## Still required before end-to-end support can be claimed

- An independently usable, revision-correct MAC/PHY/RF programming specification and validated eFuse/board-calibration mapping.
- Enable radio TX only after successful initialization and calibration, with stage-specific error reporting.
- Verify actual channel tuning, scan results, auth/association, WPA2 handshake, CCMP data transfer, and DHCP/DNS against a physical adapter and WPA2-PSK 2.4 GHz AP.
- Add tests for the new station/data path and validate descriptor/FCS semantics against real hardware. Host tests cannot prove RF behavior.


## Validation in this change

The RTL8188EU mocked-device test checks observable state transitions, firmware-download ordering, passive beacon parsing, and that scan/raw TX fail closed while radio initialization is absent. The 802.11 host test exercises Open System Authentication request construction, Authentication Response parsing, Association Request construction with a bounded RSN IE, Association Response/status/AID parsing, and bounded Ethernet/LLC-SNAP conversion, including malformed/truncated inputs. CCMP host tests cover the AES-CCM known-answer vector, tamper handling, packet-number allocation/exhaustion, header/nonce encoding, and out-of-order/replayed/too-old packet numbers. WPA2 tests also cover invalid reconnect cleanup. These tests validate selected components only; they do not establish live association, secured data transfer, or physical hardware functionality.