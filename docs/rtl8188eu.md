# RTL8188EU bring-up status

The experimental RTL8188EU USB driver currently identifies supported USB IDs, validates the configuration descriptor's declared extent and endpoint-address fields, parses bulk endpoints only from alternate setting zero, probes the Realtek register interface, and attempts a bounded firmware download from `/SYSTEM/RTL8188EU.BIN` after FAT32 is mounted. Firmware download is limited to the eight 4-KiB pages addressable by the chip's page selector, and checksum completion is treated as a device-reported status rather than a software-set success bit. The supplied file must be the matching RTL8188EU firmware image with its 32-byte header intact. Firmware is not bundled in this repository.

This driver is still **not a working Wi-Fi network device**. The latest branch now includes RF serial register access and channel-register readback, poll-driven channel-hopping scan logic, live Open System Authentication/Association state handling, WPA2 EAPOL callbacks connected to received frames and USB transmit, CCMP data-frame protection/replay checks, Ethernet/LLC-SNAP bridging, a conditional `net_device_t` registration path, and nonblocking DHCP startup after WPA2 authorization.

The radio-ready gate remains closed unless the system supplies a valid `/SYSTEM/RTL8188EU.PHY` profile matching the detected chip cut. The profile must provide MAC, baseband, AGC, TX-power, and IQ-calibration phases, explicitly select 20-MHz RF mode, and pass bounded polling/readback checks. The driver now decodes the RTL8188E eFuse CCK/HT40 power groups and signed OFDM/HT20 deltas, programs the path-A TXAGC registers for CCK, legacy OFDM, and MCS0–7, and re-applies the channel-specific values with register readback. This is meaningful initialization work, but the repository still does not ship a verified board-specific PHY profile; a profile-less build remains fail-closed. TXAGC register readback is not proof of calibrated RF output, and none of these paths has been validated on a physical adapter.

## Validation status

The host regression test covers unsupported IDs, truncated/malformed configurations, reserved endpoint-address bits, endpoint binding, mocked firmware upload/ready signaling, passive beacon parsing, and disconnect cleanup. These are software-only tests against mocks; they do not validate USB timing, radio initialization, RF calibration, packet exchange, or Internet connectivity on physical hardware.

The branch is still a bring-up effort, not a complete or production-ready Wi-Fi driver. In particular, do not report the adapter as connected merely because USB binding or firmware upload succeeds.


## Firmware-download safeguards

- The firmware image must include the Realtek 32-byte header and match the exact RTL8188EU revision.
- The loader rejects images larger than the currently supported eight-page download window instead of wrapping the page selector.
- The download sequence waits for the hardware checksum-report status after disabling download mode; software does not set that status bit to manufacture success.
- The host mock models the checksum-report transition only after download is disabled, so regression tests catch the previous false-success behavior.

These safeguards improve firmware bring-up correctness; they do not complete the radio driver. The management, WPA2, CCMP, network-device, and DHCP software paths have now been connected in code, but remain gated behind verified MAC/PHY/RF initialization and calibration. No real connection has been validated.

## Supplying firmware to the boot image

The firmware image is intentionally not vendored here. Obtain a matching RTL8188EU firmware image under its upstream redistribution terms, then build the FAT32 disk image with the path explicitly supplied:

```sh
RTL8188EU_FIRMWARE=/path/to/rtl8188eufw.bin ./build.sh
```

The build script checks that the file exists and is within the current size bounds, then installs it as `/SYSTEM/RTL8188EU.BIN`. Without this variable, the build continues but reports that firmware is absent. This only provisions the file; it does not imply the driver has completed radio initialization or supports an authenticated network connection.
## USB transmit primitive

The driver now exposes `rtl8188eu_transmit_frame()` for a complete 802.11 management or data MPDU (without FCS). It builds the 32-byte RTL8188E USB TX descriptor, chooses the management or best-effort queue, submits through the discovered bulk-OUT endpoint, and requires the full transfer length to complete. Host tests validate the descriptor fields and ensure a too-short frame is rejected.

This is a low-level transport primitive. RF register access and channel programming, authentication/association state handling, WPA2 EAPOL, CCMP framing, and network bridging now exist in code, but are gated until the missing full MAC/PHY/RF initialization and calibration are implemented. A successful mocked transfer is not evidence that a physical adapter can transmit over the air.

## Experimental WPA2 crypto primitives

The branch now contains bounded PBKDF2-HMAC-SHA1 PMK derivation and an AES-CCM primitive with host known-answer tests. The CCMP host test checks successful encrypt/decrypt, MIC tampering, ciphertext tampering, AAD tampering, and zeroing of the output buffer after authentication failure. These tests exercise the primitive's failure behavior, not 802.11 CCMP nonce/AAD construction or key/replay management. These are prerequisites only. They are not wired into an EAPOL four-way handshake or the RTL8188EU RX/TX path, and do not establish that CCMP nonce/AAD construction, replay protection, key installation, or packet exchange works on hardware. Do not advertise WPA2 connectivity on the basis of these unit tests.

The AES implementation is newly written and has not undergone an independent security audit. Treat it as experimental; it is not a substitute for a reviewed cryptographic implementation.
