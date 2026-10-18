# RTL8188EU bring-up status

The experimental RTL8188EU USB driver currently identifies supported USB IDs, validates the configuration descriptor's declared extent and endpoint-address fields, parses bulk endpoints only from alternate setting zero, probes the Realtek register interface, and attempts a bounded firmware download from `/SYSTEM/RTL8188EU.BIN` after FAT32 is mounted. Firmware download is limited to the eight 4-KiB pages addressable by the chip's page selector, and checksum completion is treated as a device-reported status rather than a software-set success bit. The supplied file must be the matching RTL8188EU firmware image with its 32-byte header intact. Firmware is not bundled in this repository.

This is not a working Wi-Fi network device yet. MAC/PHY/RF setup and calibration, RX/TX descriptors and queues, 802.11 scanning and association, WPA2 key negotiation and CCMP, and the network-device/IP bridge remain incomplete. CI descriptor tests do not prove hardware functionality. Physical validation requires a supported adapter and access point.

## Validation status

The host regression test covers unsupported IDs, truncated/malformed configurations, reserved endpoint-address bits, endpoint binding, mocked firmware upload/ready signaling, passive beacon parsing, and disconnect cleanup. These are software-only tests against mocks; they do not validate USB timing, radio initialization, RF calibration, packet exchange, or Internet connectivity on physical hardware.

The branch is still a bring-up effort, not a complete or production-ready Wi-Fi driver. In particular, do not report the adapter as connected merely because USB binding or firmware upload succeeds.


## Firmware-download safeguards

- The firmware image must include the Realtek 32-byte header and match the exact RTL8188EU revision.
- The loader rejects images larger than the currently supported eight-page download window instead of wrapping the page selector.
- The download sequence waits for the hardware checksum-report status after disabling download mode; software does not set that status bit to manufacture success.
- The host mock models the checksum-report transition only after download is disabled, so regression tests catch the previous false-success behavior.

These safeguards improve firmware bring-up correctness; they do not complete the radio driver. No supported production connection path should be exposed until MAC/PHY/RF calibration, TX and RX handling, active channel scan, association, WPA2-PSK/CCMP, and network-device integration are implemented and tested.
