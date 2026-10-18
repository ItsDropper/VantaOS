# RTL8188EU bring-up status

The experimental RTL8188EU USB driver currently identifies supported USB IDs, parses bulk endpoints, probes the Realtek register interface, and attempts a bounded firmware download from `/SYSTEM/RTL8188EU.BIN` after FAT32 is mounted. The supplied file must be the matching RTL8188EU firmware image with its 32-byte header intact. Firmware is not bundled in this repository.

This is not a working Wi-Fi network device yet. MAC/PHY/RF setup and calibration, RX/TX descriptors and queues, 802.11 scanning and association, WPA2 key negotiation and CCMP, and the network-device/IP bridge remain incomplete. CI descriptor tests do not prove hardware functionality. Physical validation requires a supported adapter and access point.