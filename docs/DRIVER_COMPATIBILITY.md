# Driver compatibility roadmap

Copyright (c) 2026 ItsDropper

## Current driver surface

- PCI discovery and a registered PCI network-driver framework.
- Wired network drivers: Intel E1000, Realtek RTL8139, and virtio-net.
- USB host support through xHCI; HID boot keyboard and mouse enumeration.
- USB Wi-Fi work for Realtek RTL8188EU. See RTL8188EU_WIFI.md for the exact limitations; firmware readiness is not proof of association or Internet access.
- ATA and NVMe storage paths, plus block-device and partition layers.
- USB Bluetooth HCI transport binding configures interrupt-IN event and bulk ACL endpoints, issues HCI Reset through the class control pipe, polls HCI events, queues validated ACL-IN packets in a bounded per-device queue, and exposes raw ACL receive/send APIs after a successful Reset Command Complete. This is transport bring-up only, not a complete Bluetooth stack; pairing, L2CAP, security, profiles, and end-to-end connectivity remain unsupported. See [the bring-up course lesson](USB_BLUETOOTH_BRINGUP.md).

## Bluetooth implementation status

`drivers/usb/bluetooth.c` validates the full configuration descriptor and identifies the expected HCI interrupt-IN event endpoint plus bulk-IN and bulk-OUT ACL endpoints. `drivers/usb/usb.c` now configures those xHCI endpoints, sends HCI Reset through the class control pipe, starts event reception and re-arms a bounded ACL-IN receive after completion. Port disconnect marks the transport inactive and releases the xHCI slot. The descriptor parser is covered by `scripts/test_usb_bluetooth_descriptors.c`.

This is a transport milestone, not a complete Bluetooth implementation. Bounded raw ACL receive and transmit paths are present and gated on successful HCI Reset completion; incoming packets are queued rather than immediately discarded. L2CAP, pairing/security, device discovery, profile support, and an end-to-end Bluetooth API remain unimplemented. Serial event markers prove only that the event path was reached; they do not prove a controller completed reset successfully or that any Bluetooth profile works. Physical-adapter and QEMU controller testing remain required.

## Wi-Fi adapter versus router

A Wi-Fi adapter driver controls the local radio and sends/receives 802.11 frames. A router or access point is normally the other end of the connection and does not need its own VantaOS driver. Compatibility depends on the adapter chipset, radio modes, regulatory channel policy, authentication, encryption and network stack. Select additional adapters by exact chipset and revision, not retail brand alone.

## Clipboard and terminal

The graphical terminal now retains up to 512 output lines in a ring buffer. At capacity, adding a line advances the oldest-line index rather than copying the whole history. PageUp and PageDown navigate the retained output.

Host clipboard access is not implemented. Ctrl+V cannot read the Windows clipboard from inside the guest without an explicit clipboard ABI and a supported host or VM bridge. Developer paste scripts are not a guest clipboard service.

## Driver acceptance checklist

- Validate vendor/product IDs and all device descriptors before binding.
- Bound DMA buffers, packet sizes, queues and poll budgets.
- Keep waits bounded and do not block in interrupt context.
- Clean up outstanding transfers on failed probe and disconnect.
- Report discovered, bound, initialized, linked and usable as distinct states.
- Test descriptor parsing, failures and cleanup in CI; report physical-hardware results separately from emulator results.


USB Bluetooth's raw ACL send and receive interfaces are gated on a successful HCI Reset event. A descriptor match alone is not a ready transport. Queue dequeue also revalidates stored length metadata before copying a packet to a caller buffer.
