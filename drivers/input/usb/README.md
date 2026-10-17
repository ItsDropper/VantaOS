# USB input drivers

## Implemented: HID boot-report decoder

`hid.c` implements USB HID boot-protocol keyboard and mouse report handling.
It tracks key transitions and modifier state, emits normalized key events,
and reports mouse motion, button transitions, and wheel deltas through the
shared input core. Each attach operation registers a USB-bus device; detach
unregisters it and clears queued events. The decoder has fixed capacities and
does not allocate memory.

The transport must call attach only after USB enumeration and successful
selection of the relevant HID boot protocol, then pass complete reports to the
matching report function. The decoder does **not** enumerate USB devices or
talk to an xHCI controller by itself.

## Remaining USB stack work

1. PCI discovery and xHCI controller initialization.
2. Controller DMA-safe memory allocation/mapping, command and event rings,
   transfer rings, and interrupt/polling completion.
3. USB port reset, device/address/configuration enumeration, endpoint setup,
   and boot-protocol selection.
4. Connect completed interrupt-IN transfers to these report functions.

Until that transport exists, USB HID devices are not discovered or usable at
boot. Existing PS/2 drivers remain the active input path under
`drivers/input/ps/2/`.
