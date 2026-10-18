# Network adapter support

VantaOS discovers PCI Ethernet controllers through the network-driver registry. Current in-tree drivers have deliberately narrow compatibility claims:

| Driver | Current target | Transport |
| --- | --- | --- |
| `e1000` | Listed legacy Intel 8254x IDs that share the implemented register/descriptor model | PCI MMIO, DMA polling |
| `virtio-net-legacy` | Legacy/transitional VirtIO network device ID `1af4:1000` | PCI I/O ports, legacy split virtqueues |
| `rtl8139` | Realtek RTL8139 PCI Ethernet, vendor/device `10ec:8139` | PCI I/O ports, DMA ring, bounded polling |

The RTL8139 driver supports Ethernet frame TX/RX, feeds received frames into the existing common Ethernet/network stack, updates link state from the media-status register, and uses a bounded poll budget. GitHub Actions runs it in QEMU against a socket-backed network to exercise transmit, ARP, ICMP, and UDP receive paths.

## Not universal networking

A PCI network-driver registry is not a universal driver. These drivers do not imply support for every Intel/Realtek/Broadcom/Qualcomm/MediaTek NIC, modern-only VirtIO PCI, USB Ethernet adapters, Thunderbolt networking, cellular modems, or physical Wi-Fi. Unsupported device IDs must remain unbound rather than being probed with an incompatible register model.

Physical Wi-Fi still requires a chipset-specific driver, firmware handling where applicable, 802.11 management/data support, and an authenticated WPA2/WPA3 implementation. The generic Wi-Fi control API alone does not provide those components. HTTPS/TLS is also not enabled; see [secure networking](secure-networking.md).
