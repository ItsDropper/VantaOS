# Networking architecture

This document defines the initial networking contracts. It does not claim a NIC driver or IP connectivity exists yet.

## Layering

    User-mode socket/syscall ABI (future)
                |
    TCP / UDP / DHCP / DNS / ICMP / IPv4 (future protocol modules)
                |
    Ethernet-II demultiplexer (implemented)
                |
    Bounded RX queue + network-device registry (implemented)
                |
    PCI network-driver registry (implemented)
                |
    NIC driver (E1000/QEMU target next; not implemented yet)
                |
    PCI, DMA, interrupts, PMM/paging (existing platform services)

Architecture-independent code lives in net/; PCI-bound hardware drivers live in drivers/network/; shared contracts live in include/net.h. The registry probes only PCI class 0x02 devices. Drivers declare IDs or wildcard fields and register before probing starts.

## Device lifecycle

1. A driver initializes its controller, DMA descriptors, and interrupt state.
2. It constructs a persistent net_device_t, supplies a valid unicast MAC address, an MTU from 576 through 1500, and synchronous transmit operations.
3. net_device_register() publishes the device. Its descriptor and ops must remain alive until net_device_unregister() succeeds.
4. Link transitions use net_device_set_link(); transmit is rejected while link is down.
5. Unregister fails while receive frames for that device remain queued, preventing dangling queue pointers.

Transmit is synchronous at this boundary: a driver must copy bytes into its own descriptor/buffer before returning. A future zero-copy interface must define DMA pinning, completion, cancellation, and ownership explicitly.

## Receive and execution context

net_receive_frame() validates bounds and copies into a fixed-capacity queue. It calls no protocol handlers and allocates no memory, so it can be called from interrupt context. Queue overflow increments per-device drop statistics. Handlers run only from net_poll() in caller context; a future scheduler-owned network worker should drive polling.

Ethernet-II parsing validates the base header and supports one 802.1Q/802.1ad VLAN tag. Nested tags and IEEE 802.3 length-encoded frames are rejected. Handlers receive the complete frame, decoded EtherType, and header length; they must not retain the frame pointer after returning.

## Concurrency and resource bounds

- Device, handler, PCI driver, binding, and RX registries have fixed capacities.
- Registry mutations use IRQ-save spinlocks. Driver and protocol callbacks run without the core registry lock held.
- RX performs bounded copies into static storage and does not call kmalloc() in interrupt context.
- Device unregister is refused if queued RX entries still reference that device.
- Statistics are lock-protected snapshots, not a stable userspace ABI.

## PCI driver contract

A net_pci_driver_t supplies a persistent name, PCI match IDs, and a probe callback. Probe results are NOT_MINE, BOUND, or FAILED; successful bindings are recorded to avoid repeated probes. Concrete drivers must validate BAR types/ranges, enable memory decoding and bus mastering only after DMA setup, manage interrupts, and unwind partial initialization.

## Current limitations

No concrete NIC driver, ARP, IPv4, ICMP, DHCP, DNS, UDP, TCP, routing, sockets, or network syscalls are implemented. CI exercises the core registry, TX contract, bounded RX queue, statistics, and Ethernet demultiplexing with a mock device; this does not prove external connectivity.

## Next sequence

1. Add an E1000-compatible QEMU PCI driver with bounded DMA rings and link detection.
2. Add QEMU user-network end-to-end packet tests.
3. Implement ARP and IPv4 validation/checksums, then ICMP echo and a QEMU ping test.
4. Add DHCP and DNS over UDP, then general UDP and TCP.
5. Define socket syscalls after protocol and buffer-lifetime contracts are tested.
