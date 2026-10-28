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

The test suite checks a valid endpoint set, an incomplete endpoint set, truncated/malformed descriptor lengths, a malformed tail after an otherwise valid interface, invalid configuration headers/total lengths, null arguments, and a non-Bluetooth interface. These are parser tests, not physical-device tests.

Do not turn a failed match into a fatal USB enumeration error. An unsupported interface should remain unbound, and other interfaces/devices must continue to enumerate.

## What comes next

A real driver needs explicit interface claiming, endpoint configuration, bounded transfer completion, interrupt-IN event handling, bulk ACL transport, HCI command/event correlation, reset/recovery, and disconnect cleanup. Only after those layers pass emulator and supported-hardware tests should VantaOS advertise Bluetooth functionality.


## Transport binding milestone

The USB enumerator now uses a validated candidate to configure the interrupt-IN event endpoint and both bulk ACL endpoints through the xHCI endpoint configuration API. It sends the HCI Reset command (`0x0C03`) using a USB class control request, starts interrupt event reception, and submits one bounded bulk-IN ACL receive. The polling loop consumes completed event/ACL-IN transfers and re-arms ACL reception.

The HCI Reset request uses `bmRequestType = 0x20`, `bRequest = 0`, `wValue = 0x0C03`, `wIndex = interface number`, and zero data length. HCI commands are not sent through the ACL bulk endpoint. The event path records first-event and first Command Complete/Command Status markers; a production HCI layer must still validate event lengths, the returned opcode, status, and controller state.

The bulk-OUT endpoint is configured, and the kernel exposes a bounded raw-ACL transmit API plus a queued raw-ACL receive API. These are transport primitives, not an HCI command scheduler or a complete Bluetooth stack. The current loop is not a pairing or profile stack. Treat this as an incremental transport milestone only, and validate it on an emulated xHCI Bluetooth device and physical adapters before advertising compatibility. CI validates host-side descriptor, framing, queue, and reset-state logic plus selected build/QEMU regressions; it does not prove physical Bluetooth compatibility.


## Packet framing checks

Before counting an interrupt transfer as an HCI event, the polling path verifies that the event's parameter-length byte exactly matches the transfer length minus the two-byte event header. The HCI Reset success marker is emitted only for a Command Complete event carrying opcode `0x0C03` and status zero. Receiving an unrelated event or a Command Status event is not considered a completed reset.

ACL-IN packets are validated against the four-byte ACL header: the little-endian data length must equal the received transfer length minus four. Truncated headers, short payloads, and trailing bytes are rejected by the framing helper. These checks are transport-level; a future HCI layer still needs to validate handle/flags and route valid payloads into L2CAP.

The descriptor workflow now tests valid, truncated, overlong, wrong-opcode, failed-reset, and malformed ACL packets using strict compiler warnings. This verifies parser/framing logic only; it does not emulate a USB controller or prove that a physical adapter will deliver packets.

## Bounded ACL transmit API

The kernel now exposes `usb_bluetooth_acl_send(port, packet, length)` for the transport layer. It accepts exactly one framed ACL packet, checks the four-byte header and declared little-endian payload length, rejects packets larger than the current 4096-byte xHCI bulk buffer, copies into a per-slot DMA-independent staging buffer, and submits the configured bulk-OUT endpoint. Success is returned only when xHCI reports a full-length transfer; malformed, inactive, busy, failed, or short transfers return zero.

The call is synchronous and deliberately has a narrow execution contract: invoke it only from the USB polling/owner CPU in ordinary kernel context, not from an interrupt handler or another CPU. USB port disconnect and xHCI slot teardown are serialized by that same owner path. The per-slot busy flag rejects nested/re-entrant sends, but it is not a substitute for SMP-safe device lifetime management; if USB work becomes multi-CPU, add a device reference/lock protocol before relaxing this restriction.

This API only transmits raw ACL packets. It does not create L2CAP frames, discover peers, pair devices, or implement profiles. The existing host regression suite validates the shared ACL framing predicate, while actual transfer completion still requires xHCI emulation and hardware testing.


## Reset state and transmit gating

The event path distinguishes three outcomes for the HCI Reset Command Complete event: success (status zero), controller-reported failure (nonzero status), and an unrelated or malformed event. Only the matching Reset opcode with a valid event length changes reset state. A failed reset emits `VANTAOS_BLUETOOTH_HCI_RESET_FAILED`; it is not treated as initialized. The raw ACL send API refuses to transmit until a successful Reset Command Complete has been observed.

On disconnect, the USB owner path clears the slot's Bluetooth endpoint/state fields and transmit-busy flag before disabling the xHCI slot. Repeated connect-change notifications also check Bluetooth bindings, avoiding duplicate enumeration of a port already bound to a Bluetooth interface. These rules reduce stale-slot reuse, but do not replace a future reference-counted or locked device-lifetime model if USB work moves to multiple CPUs.

The host tests exercise reset-status classification and framing predicates. They do not test event delivery timing, controller recovery, ACL transfer completion, or hardware disconnect races.


## Bounded ACL receive queue

Valid ACL-IN packets are now copied into a two-entry per-device queue instead of being discarded after validation. The kernel-facing `usb_bluetooth_acl_receive(port, packet, capacity)` returns one complete raw ACL packet length, `-11` when the queue is empty, `-90` when the destination is too small, and `-9` when no Bluetooth transport is bound to that port. A too-small destination does not consume the queued packet. When the queue is full, the incoming packet is dropped rather than overwriting older unread data; the first overflow emits `VANTAOS_BLUETOOTH_ACL_QUEUE_FULL`.

The queue is deliberately bounded (two packets per slot) to keep memory use predictable. Push/pop and device lifecycle are currently intended for the USB owner CPU; this is not an SMP-safe multi-consumer queue. The host regression tests cover FIFO order, full-queue behavior, malformed packets, empty queues, and retrying after a too-small buffer. They do not establish that a physical controller supplies ACL data or that a higher protocol can use it.


## Queue integrity and payload clearing

The ACL queue rejects corrupted ring metadata before calculating an array index: the head must be within the two-slot ring and the count cannot exceed its depth. Reset clears both lengths and stored packet bytes; dequeue clears the consumed slot after copying it out. These rules limit stale packet data surviving disconnect/rebind and make malformed queue state fail closed. The host suite covers ring wraparound, invalid head/count values, FIFO order, capacity retry, full queues, and reset clearing. The queue still requires the documented single-owner CPU context.


## Transport readiness and queue corruption handling

The raw ACL send and receive entry points share a readiness predicate: neither is exposed to callers until a successful HCI Reset Command Complete has been observed, and a reset failure keeps the transport unavailable. The receive API returns the existing retryable empty/not-ready result until the controller reaches that state. Duplicate reset-failure events emit only one failure marker per device binding.

Queue dequeue now validates the stored length and packet framing before reading from the fixed-size packet buffer. Corrupted length metadata fails closed without changing the queue, preventing a damaged length field from turning into an out-of-bounds read. Host tests cover both an oversized stored length and a length inconsistent with the ACL header.


## Polling efficiency and failure markers

The polling loop now discards ACL completions until HCI Reset has succeeded, rather than retaining pre-initialization packets for later delivery. A controller's repeated failed-reset events produce a single diagnostic marker for the binding. Dequeue clears only the validated bytes of the consumed packet instead of sweeping the full 4096-byte slot on every read; reset/disconnect still wipes the complete queue storage.


## Unified binding teardown

Bluetooth binding teardown is centralized so disconnect and a partially failed endpoint-start sequence clear the same state: port/interface/endpoints, counters, reset readiness, transmit-busy state, dropped-packet count, and the full ACL queue. Binding initializes counters and reset state explicitly before starting transfers. This prevents stale per-slot state from surviving a failed start and being mistaken for the next device binding.


## Unit-tested reset state transitions

HCI Reset Command Complete processing is factored into a pure state-transition helper. It distinguishes unrelated events, the first successful reset completion, and the first nonzero reset status; duplicate events do not emit duplicate markers. Tests exercise failure followed by success, duplicate failure, duplicate success, unrelated opcodes, null state pointers, and the resulting readiness gate.


## Endpoint limits

Descriptor matching now rejects unusable event endpoints with a zero polling interval or a maximum packet size larger than the fixed 64-byte HCI event staging buffer. Bulk ACL endpoints with packet sizes above 1024 bytes are not accepted. This is a conservative transport limit, not a claim of support for every USB speed or controller; descriptors outside the driver's explicit buffer contract remain unbound rather than risking an oversized endpoint configuration.


## Descriptor fuzz-style regression and sanitizers

The descriptor regression iterates every byte position in the canonical configuration and substitutes all 256 byte values. This is a deterministic mutation test, not a coverage-guided fuzzer: its purpose is to exercise descriptor-length, type, endpoint-address, and header checks repeatedly while keeping the input allocation fixed. For every rejected input, the output candidate must remain zeroed; a failed parse must never leak a partial interface match.

The GitHub Actions workflow runs the same suite twice: once with strict C11 warnings treated as errors, and once with AddressSanitizer and UndefinedBehaviorSanitizer enabled. These host-side checks can detect memory-safety defects in the parser and helpers, but they cannot model xHCI DMA, transfer timing, interrupts, controller firmware, radio behavior, or hot-unplug races. Keep those claims separate from test results.


The endpoint parser also rejects upper `wMaxPacketSize` bits that would encode high-bandwidth transactions or reserved values. This transport configures ordinary bounded interrupt/bulk transfers only; it must not silently interpret a multi-transaction endpoint as a single packet with the same low 11-bit size. A negative descriptor test covers this case.


## HCI framing mutation coverage

The sanitizer-backed host suite now mutates every byte of packets at every length from zero through the 64-byte staging-buffer limit. Accepted HCI event and ACL packets must have a declared payload length that exactly matches the supplied transfer length. This is deterministic mutation testing of framing validators, not a claim of coverage-guided fuzzing or hardware-level transfer validation.


## L2CAP framing groundwork

The host-tested protocol helpers now decode the HCI ACL handle, packet-boundary
flag, broadcast flag, and payload view, then validate a single complete L2CAP
PDU's declared length and channel identifier. A separate validator checks the
full list of L2CAP signaling command headers and payload lengths before any
future dispatcher processes commands. These helpers deliberately reject ACL
continuation fragments as standalone L2CAP PDUs: correct fragment reassembly,
connection/channel state, pairing, encryption, service discovery, and profiles
remain unimplemented. This is protocol framing groundwork, not a claim that
Bluetooth is complete or usable for end-user connections.


## USB disconnect recovery and transfer re-arming

The USB owner loop now defers a slot whose Disable Slot command fails and retries
it on later polls. This prevents a transient command timeout during disconnect
or partial enumeration from permanently consuming one of the controller's
limited device slots. Bluetooth ACL-IN polling also retries a missing
in-flight submission when no completed packet is being preserved. A capacity
shortage (`-2`) is deliberately excluded from re-arming because the completed
packet remains in the DMA buffer until the caller can consume it. These
retries are bounded per poll and do not reset the controller or prove that a
controller which stopped responding can recover; that still requires xHCI
emulation and hardware testing.


## HCI command access and event delivery

The USB transport now queues complete, framing-validated HCI events in a
four-entry per-device FIFO while still processing Reset Command Complete
internally. Callers can drain events through
`usb_bluetooth_hci_event_receive`; a small destination does not consume the
event, and overflow drops the new event rather than overwriting older events.
The `usb_bluetooth_hci_command` API sends a bounded command through the USB
class control pipe, permits only Reset before transport readiness, and clears
stale queued events/ACL data when a new Reset is requested. It intentionally
does not implement HCI command-credit scheduling: callers must serialize
commands on the USB owner CPU and wait for the matching completion/status event.
Host tests cover event FIFO order, full/empty behavior, framing rejection,
capacity retry, and queue erasure.


## ACL fragment reassembly

A bounded, host-testable reassembly primitive now handles one HCI connection
handle per state object. It validates the L2CAP length before accepting data,
rejects handle mismatches and overrun fragments, retains completed packets
when the consumer buffer is too small, and clears the full buffer on reset or
successful take. Callers must allocate separate state per live connection;
the USB polling path does not yet create or manage L2CAP channels, so this
primitive alone does not enable discovery or profiles.


The reassembly API also validates its public state fields before arithmetic or
buffer writes. Invalid lengths, impossible active/inactive combinations, a
second first-fragment while a PDU is in progress, and continuation overruns
are rejected without discarding the valid partial packet. State objects must
be initialized with `usb_bluetooth_acl_reassembly_reset` before first use.
