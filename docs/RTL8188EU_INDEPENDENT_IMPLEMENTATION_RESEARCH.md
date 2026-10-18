# RTL8188EU independent implementation research

Status: research baseline for PR #146. This is an engineering plan, not a claim that the hardware works.

## No-third-party-code rule

VantaOS implementation code must be authored independently. Public driver sources may be consulted to understand hardware behavior and cross-check register semantics, but no third-party implementation code, register-table arrays, calibration routines, or firmware blobs are copied into VantaOS. The Linux RTL8XXXU source is explicitly GPL-2.0-only and contains Realtek-copyrighted calibration portions, so copying its tables or routines into this MIT project would create a licensing problem. Upstream source is a behavioral reference only.

Knowing the initialization phases is not enough to invent safe register values. RTL8188EU needs device-specific PHY/RF setup and calibration; guessed writes must not be represented as functional support.

## Findings

### 1. The RTL8188EU is a USB SoftMAC WLAN chipset

A successful USB match only binds transport. A firmware-ready bit does not establish that the baseband/RF is initialized, calibrated, tuned to a channel, associated to an AP, authorized by WPA2, or able to carry IP.

VantaOS needs distinct layers:
- USB/register/firmware transport;
- MAC/PHY/RF bring-up and radio configuration;
- bounded 802.11 management and data-frame engine;
- station authentication/association state machine;
- WPA2-PSK four-way handshake and key lifecycle;
- CCMP encapsulation/decapsulation and replay protection;
- 802.11 LLC/SNAP conversion to/from Ethernet-like network frames;
- registered network device, followed by DHCP/DNS/IP.

### 2. Hardware bring-up is a sequence, not one register write

Public RTL8188E driver documentation/source identifies distinct phases: power-state transitions, MCU reset and firmware load, eFuse read/parse, MAC initialization, linked-list/packet-buffer setup, baseband and AGC initialization, RF-path setup, transmit-power configuration, crystal-cap adjustment, RF channel configuration, and LC/IQ calibration. These phases have ordering dependencies.

The upstream implementation has chip-cut detection and refuses a revision it does not support. VantaOS should validate chip/revision and eFuse-derived configuration before transmitting. If a revision, required eFuse field, RF path, or firmware image is unknown, fail closed and report the failing stage.

The existing firmware download and firmware-ready polling are necessary but insufficient. The current-channel Probe Request helper must not run until initialization explicitly reaches radio-ready state.

### 3. eFuse and board calibration are essential

Read and decode factory-programmed identity/configuration with bounds checks. Validate at minimum:
- permanent MAC address;
- chip cut/version and USB/board variant;
- RF path count and board/regulatory data;
- crystal/capacitor trim;
- per-channel transmit-power indexes and differences;
- thermal, LC and IQ calibration prerequisites and results.

A MACID register read can provide a runtime MAC value, but it is not a substitute for validating eFuse identity and board configuration. Do not synthesize calibration values or transmit-power tables. If eFuse data is invalid, do not transmit until a fallback is justified by reliable device documentation.

### 4. Channel setting must work before scanning

A scan requires a supported-channel list and channel-programming routine. For each permitted channel, the driver needs to validate the channel and regulatory range, program RF synthesizer state, apply channel-dependent power, wait for or verify completion where possible, send a probe request, receive and parse beacons/probe responses with bounded IE parsing, and advance with bounded timeout/cancellation.

The current probe helper emits a frame on whatever channel the radio already uses. It is not a scan. Start with verified supported 2.4 GHz channels; do not claim 5 GHz support for RTL8188EU.

### 5. TX/RX is more than sending an MPDU

USB bulk transport uses chipset-specific TX/RX descriptors and RX aggregates. Validate descriptor lengths, packet boundaries, reported lengths, alignment, status flags and aggregate offsets before exposing a frame. A successful USB submission only means the transfer was accepted by the host controller; it does not prove that the AP acknowledged the frame. Where supported, TX reports/ACK status must reach the management state machine.

VantaOS currently uses a 32-byte TX descriptor and parses RX descriptor/aggregate data. It still needs a testable contract for TX completion, RX delivery, disconnect, cancellation and recovery.

### 6. WPA2 requires the real 802.11 data path

The repository has standalone PBKDF2/HMAC/PRF, EAPOL key-message logic, key unwrap, and AES-CCM primitives with host tests. Those are building blocks only. The radio path must connect them without exposing key material or accepting unauthenticated traffic.

Required state flow:
1. Select a scan result with a supported RSN IE (PSK AKM + CCMP-128 for the first implementation).
2. Open-system authentication request/response with retries and deadline.
3. Association request with validated RSN IE; check response status and AID.
4. Permit only EAPOL-Key control traffic before authorization.
5. Derive PMK from passphrase and SSID, then PTK from fresh CSPRNG nonce, addresses and AP nonce.
6. Validate replay counters, nonces, descriptor version, MIC and bounded key data.
7. Transmit handshake responses and validate completion semantics.
8. Unwrap and validate GTK; install PTK/GTK atomically only after successful checks.
9. Authorize ordinary data only after success.
10. On disconnect/rekey/failure, invalidate keys, reset packet-number/replay state correctly, and clear link state.

Do not support WPA3/SAE, TKIP, WEP, enterprise 802.1X or PMF until separately implemented and tested. A narrow WPA2-only implementation is safer than silently accepting unsupported security combinations.

### 7. CCMP framing and replay defense are separate from AES-CCM

The AES-CCM primitive is not the complete CCMP protocol. The data path must build the CCMP header, derive the 13-byte nonce and canonical AAD from the 802.11 header, maintain transmit packet numbers per key/TID, reject packet-number reuse/exhaustion, verify the authentication tag before exposing plaintext, and enforce receive replay windows. It must handle To-DS/From-DS address selection and reject unsupported four-address/QoS combinations instead of constructing incorrect nonce/AAD.

LLC/SNAP decapsulation must validate the 802.11 data header and EtherType. EAPOL (0x888e) needs controlled pre-authorization handling; IP data must not flow until WPA2 authorizes the port.

### 8. Network integration and link-state rules

Use the existing net_device abstraction and APIs for registration, link state, transmit, receive, poll and DHCP. Register only after transport and MAC are valid. For secured networks, link-up should mean associated and WPA2-authorized, not merely firmware-ready or radio-ready. Ethernet frames must be converted to/from 802.11; an Ethernet frame cannot be sent directly as a Wi-Fi MPDU.

Do not run DHCP until the data port is authorized. Disconnect, failed handshake, USB removal, firmware fault or RF fault must drop link, cancel pending work, clear connection state and invalidate keys.

## Independent implementation sequence

1. Audit transport/chip identity. Add typed register access with width/range checking, bounded polling/deadlines, eFuse read/parse, chip-cut validation and mocked register tests. No RF transmit yet.
2. Implement documented power/MCU/firmware/MAC/BB/RF phases only after register semantics and chip variant are verified. Each stage returns an error and records the last completed stage.
3. Implement channel programming and calibration-dependent channel power; test channel validation, order, timeouts, scan cancellation, malformed IEs and result de-duplication.
4. Connect actual TX completion/RX delivery to authentication/association with deadlines, retry limits, BSSID/source checks, sequence handling and disconnect cleanup.
5. Adapt the existing WPA2 supplicant callbacks to radio-backed EAPOL transport. Production must require a real kernel CSPRNG and fail closed without entropy.
6. Implement CCMP header/nonce/AAD, PN counters and replay protection, then LLC/SNAP and Ethernet conversion. Add known-answer and malformed-frame tests.
7. Register the device and expose authorized link state; test DHCP only after successful handshake.
8. Validate with a real USB adapter and WPA2-PSK 2.4 GHz AP. Record adapter model/revision, firmware, eFuse evidence, scan, association, DHCP lease, DNS lookup and real IP transfer. QEMU/mocks cannot prove RF functionality.

## Test matrix

- Register I/O: timeout, USB error, unsupported width, boundary addresses, read-modify-write preservation.
- Identity: valid/corrupt eFuse, invalid MAC, unsupported chip cut, absent/wrong firmware, interrupted upload.
- Radio: stage ordering, calibration timeout, invalid channel, supported channel mappings, no transmit before radio-ready.
- 802.11: malformed/truncated IEs, wrong BSSID/source, denied status, duplicate/stale frames, retry/deadline/cancel.
- WPA2: wrong passphrase, invalid MIC, replayed EAPOL, malformed KDE, key-install failure, RNG failure, handshake timeout.
- CCMP: known-answer vectors, wrong AAD/nonce, tag tampering, replayed PN, PN exhaustion, QoS/TID separation, address mapping.
- Data path: EAPOL before authorization, IP blocked before authorization, LLC/SNAP conversion, MTU bounds, DHCP after authorization, disconnect cleanup.
- Hardware: actual adapter/AP test. Host tests are necessary, not sufficient.

## Research references (behavioral/standards reference only)

- IEEE 802.11 Working Group and standards access: https://www.ieee802.org/11/ and https://standards.ieee.org/ieee/802.11/7028/
- Linux Wireless documentation on SoftMAC/mac80211 architecture: https://wireless.docs.kernel.org/en/latest/en/developers/documentation/mac80211.html
- Linux kernel cfg80211 device registration, scan, crypto and regulatory concepts: https://docs.kernel.org/driver-api/80211/cfg80211.html
- RTL8188E-specific upstream driver used to understand phase boundaries, chip-cut checks, calibration dependencies, channel configuration, descriptor sizes and firmware naming (GPL-2.0-only; do not copy its implementation): https://kernel.googlesource.com/pub/scm/linux/kernel/git/netdev/net/%2B/b85966adbf5de0668a815c6e3527f87e0c387fb4/drivers/net/wireless/realtek/rtl8xxxu/8188e.c
- Public upstream common RTL8XXXU implementation (GPL-2.0-only; reference only): https://github.com/torvalds/linux/blob/master/drivers/net/wireless/realtek/rtl8xxxu/core.c

## Current VantaOS-specific reality

At the time of this research, VantaOS has a USB transport/firmware path, RX aggregate parsing, a current-channel Probe Request primitive, management frame codec tests, standalone WPA2/CCMP primitives, and a generic network-device API. These do not yet form a complete RTL8188EU radio or secure IP path. Implementation should be measured against the stages and tests above, and documentation must keep that distinction explicit.
