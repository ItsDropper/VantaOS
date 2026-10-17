# USB host and input drivers

## Implemented path

VantaOS now has an xHCI polling host-controller path for directly connected
USB 2.x/3.x root-port devices that expose the USB HID boot keyboard or mouse
protocol:

- PCI xHCI discovery, MMIO mapping, bounded controller halt/reset/run, DMA
  ring setup, and explicit bus-master enablement.
- Command and event ring processing, slot enable/address, endpoint-zero control
  transfers, device/configuration descriptor reads, and EP0 packet-size update.
- Boot-HID interface/interrupt-IN endpoint selection, SET_CONFIGURATION,
  SET_PROTOCOL, endpoint configuration, recurring interrupt transfers, and
  disconnect cleanup.
- USB keyboard reports feed both the shared input queue and the existing
  terminal/desktop keyboard buffer. USB mouse reports feed the shared queue
  and existing cursor/click/wheel path.
- Port-change polling permits supported HID devices to be attached and removed
  after boot. PS/2 remains available as a fallback.

## Boundaries

This is not a claim of universal USB-device compatibility. Current limitations:

- Root-port devices only; USB hub discovery and downstream hub enumeration are
  not implemented.
- HID boot-protocol keyboard/mouse only. Arbitrary HID report-descriptor
  parsing, gamepads, vendor-specific configuration/features, and non-HID USB
  classes are unsupported.
- The controller uses polling and static low-memory DMA rings validated against
  the current identity map. MSI/MSI-X interrupt routing and a general DMA
  allocator are future work.
- Validation must include CI/QEMU and real hardware; passing a compile does not
  prove a physical keyboard or mouse works.

The transport deliberately fails safely if required DMA structures cannot be
addressed below 4 GiB or if a controller needs more than the supported
scratchpad allocation.
