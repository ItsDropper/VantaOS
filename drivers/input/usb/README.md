# USB input drivers

## Host controller transport status

The xHCI host-controller bring-up lives in `drivers/usb/xhci.c`. It locates
PCI class 0x0C/0x03/prog-if 0x30 controllers, maps the controller BAR through
the kernel MMIO window, performs bounded halt/reset/run handshakes, sets up
the Device Context Base Address Array, command ring, event ring, and ERST,
enables PCI bus mastering only after DMA addresses are validated, and resets
connected ports. The current event path polls the event ring; interrupts are
deliberately disabled until an IRQ routing layer is available.

DMA ring storage is currently statically allocated in the low kernel image and
validated against the current identity mapping. This is a bootstrap constraint,
not the final general-purpose DMA allocator. Controllers requiring more than
64 scratchpad buffers are rejected safely.

## HID report decoder

`hid.c` handles boot-protocol keyboard and mouse reports and publishes
normalized events to the shared input core. It tracks key transitions and
modifiers, mouse movement, buttons, and wheel data. It does not yet receive
reports from hardware because USB device addressing, descriptor/configuration
enumeration, control transfers, endpoint transfer rings, and periodic interrupt
transfers still need to be connected.

## Remaining end-to-end work

1. Complete xHCI command-completion handling and endpoint transfer-ring support.
2. USB port-status handling, Enable Slot / Address Device, and descriptor-based
   enumeration.
3. Parse configuration/interface/endpoint descriptors; set configuration and
   HID boot protocol; submit recurring interrupt-IN transfers.
4. Route real transfer reports into `usb_hid_keyboard_report()` and
   `usb_hid_mouse_report()`, with disconnect cleanup and QEMU/hardware tests.

Until these pieces land, this is controller bring-up groundwork, not end-to-end
USB compatibility. PS/2 remains the active fallback input path.
