# Driver compatibility roadmap

Copyright (c) 2026 ItsDropper

## Current driver surface

- PCI discovery and a registered PCI network-driver framework.
- Wired network drivers: Intel E1000, Realtek RTL8139, and virtio-net.
- USB host support through xHCI; HID boot keyboard and mouse enumeration.
- USB Wi-Fi work for Realtek RTL8188EU. See RTL8188EU_WIFI.md for the exact limitations; firmware readiness is not proof of association or Internet access.
- ATA and NVMe storage paths, plus block-device and partition layers.
- Bluetooth is not currently a working feature.

## Bluetooth implementation plan

Start with USB Bluetooth HCI controllers, not a vendor-specific adapter guess. The first milestone is safe interface-descriptor recognition for the Bluetooth wireless-controller class, subclass and protocol. It must validate descriptor lengths and endpoint types, record the exact vendor/product/interface IDs, and leave unsupported interfaces untouched.

Recognition is not a driver. The next milestone needs xHCI control-command transport, interrupt event reception and bulk ACL transfers with bounded waits, then HCI command/event dispatch, disconnect cleanup and tests. Pairing and application support require additional L2CAP, security and profile layers. Do not claim Bluetooth works until a controller can be initialized and a supported profile passes tests.

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
