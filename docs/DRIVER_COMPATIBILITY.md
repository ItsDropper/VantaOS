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

The current kernel path includes bounded HCI event and ACL queues, controller command-credit tracking, inquiry-result discovery, outbound connection state with timeout/cancel retry, per-handle ACL reassembly, basic L2CAP signaling/channel configuration, and a public kernel-facing API for discovery, connection status, and L2CAP data. After HCI Reset, it queries Read Buffer Size, fragments outbound ACL PDUs to the controller-reported packet length, and accounts for controller ACL-buffer credits from Number Of Completed Packets events. These features establish a kernel-level Classic Bluetooth transport and basic L2CAP data path, not a desktop Bluetooth experience. Pairing, authentication, encryption, SDP, RFCOMM, HID/audio profiles, and a shipped userspace device picker are not implemented. Host CI and generic QEMU boot regressions do not establish Bluetooth hardware compatibility; a physical USB Bluetooth adapter and a controller-capable end-to-end test remain required.

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


## xHCI receive-buffer retry contract

Nonblocking interrupt-IN and bulk-IN receive functions distinguish no completed packet (`0`), success (`1`), invalid/failed transfer (`-1`), and insufficient caller capacity (`-2`). On insufficient capacity, the output length reports the required size and the completed packet remains pending; callers can retry with a larger buffer rather than silently truncating a HID report or dropping a Bluetooth ACL packet. Callers must test for a positive result before consuming output. The Bluetooth polling path only rearms bulk-IN after a failed transfer, not after a capacity-retry result.

This contract is about the driver's software ownership of completed buffers. It does not by itself provide cancellation of in-flight DMA on physical disconnect; endpoint teardown and late-event handling still require controller-level validation.


## xHCI slot teardown and receive ownership

After the controller reports successful Disable Slot completion, xHCI now clears the slot's software transfer state, endpoint rings, DMA staging buffers, pending interrupt reports, completion metadata, device/input contexts and port/speed metadata. This prevents buffered input or completion flags from a detached device being exposed when a later device reuses that slot. The state is deliberately not cleared when Disable Slot fails, because the controller may still own the DMA resources.

This cleanup is not a substitute for hardware validation of disconnect timing, controller errors, or late transfer events. It only describes the software cleanup performed after the disable command reports success.
