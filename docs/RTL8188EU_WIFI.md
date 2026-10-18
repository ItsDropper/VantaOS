# RTL8188EU Wi-Fi bring-up status (PR #146)

## What this driver currently proves

The RTL8188EU USB driver recognizes the supported USB IDs, configures bulk
endpoints, performs vendor register access, attempts to load
`/SYSTEM/RTL8188EU.BIN`, checks the firmware-download status bits, and consumes
RX aggregates to cache Beacon/Probe Response metadata. The public status API
now distinguishes USB transport binding, pending firmware loading, verified
firmware initialization, firmware-load failure, and disconnection.

`RTL8188EU_STATUS_FIRMWARE_READY` means only that the device reported its
firmware-init-ready bit. It **does not** mean the radio is calibrated, scanning
actively, associated to an access point, WPA2-authenticated, or able to carry
IP traffic. Do not use it as a network link-up signal.

## Remaining end-to-end requirements

- RTL8188EU MAC/eFuse and RF/baseband initialization, including board-specific
  calibration and power tables.
- Channel/radio configuration and active probe-request scanning.
- Open-system authentication and association request/response handling.
- A station MAC address sourced from the device, not a fabricated address.
- Secure random nonces, WPA2-PSK/CCMP supplicant connected to actual EAPOL
  transmit/receive, and atomic key installation.
- Protected 802.11 data TX/RX, CCMP packet-number replay protection, 802.11
  data/LLC-SNAP to Ethernet frame conversion, and a registered `net_device_t`.
- DHCP/DNS/TCP integration tests plus a physical RTL8188EU adapter test.

The existing WPA2 and CCMP primitives have standalone tests, but primitives
passing unit tests is not evidence that the chipset can associate or pass data.
These requirements must be implemented and tested before calling this driver
end-to-end Wi-Fi support.

## Validation added here

The RTL8188EU mock-hardware test checks status transitions from USB-bound /
firmware-pending to firmware-ready and then unbound after disconnect. This is
a truthful state contract, not a claim of completed association or Internet
connectivity.
