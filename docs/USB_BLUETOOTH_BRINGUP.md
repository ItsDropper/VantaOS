# Course lesson: recognizing USB Bluetooth HCI interfaces

Copyright (c) 2026 ItsDropper

This lesson is part of VantaOS and is licensed under the MIT License. See the repository's `LICENSE` file for the full license text.

## Goal and scope

This milestone teaches the first safe step toward a USB Bluetooth driver: identify a USB interface that advertises the Bluetooth HCI programming interface and has the endpoint shape expected for HCI events and ACL data.

It does **not** initialize an HCI controller, send HCI commands, receive real events, pair devices, or provide Bluetooth networking. A descriptor match is evidence of a candidate interface only.

## Architecture

USB enumeration belongs to the USB host layer. The host reads a device descriptor and a configuration descriptor over xHCI control transfers. A configuration contains interface and endpoint descriptors. Driver matching should consume the bounded descriptor byte stream; it must not assume that an interface exists just because a product name contains “Bluetooth”.

The parser in `drivers/usb/bluetooth.c` is deliberately independent from xHCI registers and DMA. That makes malformed descriptor cases testable on a host compiler without pretending to emulate a USB controller.

## Descriptor fields used

A Bluetooth HCI USB interface uses:

- Interface class `0xE0` (wireless controller).
- Interface subclass `0x01` (RF controller).
- Interface protocol `0x01` (Bluetooth programming interface).
- An interrupt-IN endpoint for HCI event packets.
- A bulk-IN endpoint and a bulk-OUT endpoint for ACL data.

Before walking child descriptors, the parser verifies that the first descriptor is a configuration descriptor, its header is at least nine bytes, and its little-endian `wTotalLength` exactly matches the supplied byte count. It then verifies bounds before reading every descriptor. It rejects a descriptor whose length is below the two-byte descriptor header, whose declared length runs past the provided buffer, or whose interface/endpoint descriptor is too short. This strict contract requires the caller to pass the complete configuration, not buffer capacity or a truncated prefix. Endpoint zero and addresses with reserved address bits set are ignored. A candidate is returned only when all three required endpoint roles occur on the same matching interface.

## API and example

The public declaration is in `include/usb_bluetooth.h`:

```c
usb_bluetooth_hci_candidate_t candidate = {0};
if (usb_bluetooth_find_hci_interface(configuration,
                                    configuration_length,
                                    &candidate)) {
    /* A descriptor candidate exists; no driver has been bound yet. */
    log_candidate(candidate.interface_number,
                  candidate.event_endpoint,
                  candidate.acl_in_endpoint,
                  candidate.acl_out_endpoint);
}
```

The caller must pass the actual number of bytes returned by the configuration-descriptor request, not the buffer capacity. A future integration must also ensure that the descriptor belongs to the configuration selected for the device.

## Implementation steps

1. Define a result structure containing the interface number and the endpoint addresses, packet sizes, and event polling interval.
2. Initialize every output field before parsing. This prevents callers from observing stale endpoint data after a failed match.
3. Walk descriptors using an offset and the supplied length. Before accessing a descriptor, prove that its header fits and its declared length is nonzero and in bounds.
4. On each interface descriptor, reset endpoint state and compare the three Bluetooth class fields.
5. On endpoint descriptors belonging to that interface, inspect the transfer type and direction. Record the first valid interrupt-IN, bulk-IN, and bulk-OUT endpoint.
6. Return success only after the complete required endpoint set is present on one interface.
7. Compile the parser with strict warnings and run positive, negative, and malformed-input tests.

## Tests and failure cases

Run the descriptor test as CI does:

```sh
cc -std=c11 -Wall -Wextra -Werror -Iinclude \
  drivers/usb/bluetooth.c scripts/test_usb_bluetooth_descriptors.c \
  -o /tmp/test-usb-bluetooth-descriptors
/tmp/test-usb-bluetooth-descriptors
```

The test suite checks a valid endpoint set, an incomplete endpoint set, truncated/malformed descriptor lengths, null arguments, and a non-Bluetooth interface. These are parser tests, not physical-device tests.

Do not turn a failed match into a fatal USB enumeration error. An unsupported interface should remain unbound, and other interfaces/devices must continue to enumerate.

## What comes next

A real driver needs explicit interface claiming, endpoint configuration, bounded transfer completion, interrupt-IN event handling, bulk ACL transport, HCI command/event correlation, reset/recovery, and disconnect cleanup. Only after those layers pass emulator and supported-hardware tests should VantaOS advertise Bluetooth functionality.
