# NVMe driver

The NVMe driver is a polled, kernel-mode PCI driver that exposes supported NVMe namespaces through the generic block-device registry.

## Current support

- PCI class 01h / subclass 08h / programming interface 02h discovery through the storage-driver registry.
- BAR0 MMIO mapping sized from CAP.DSTRD; controller capability validation for the NVM command set, 4 KiB minimum page size, and a 64-entry queue.
- Controller disable/ready handshake, Admin Submission/Completion Queues, Identify Controller, Create I/O Completion Queue, and Create I/O Submission Queue.
- Active namespace list and Identify Namespace parsing. Namespaces with metadata or logical block sizes outside 512–4096 bytes are rejected rather than misrepresented.
- Block-device registration with 64-bit sector counts and LBAs, one-sector DMA bounce-buffer transfers, read/write, and Flush.
- Bounded polling, completion phase tracking, command-ID checking, controller-fatal checks, and a fail-closed controller state after timeout/error.
- DMA memory is not released after a reset timeout because the controller may still own those pages.

## Deliberate limitations

- Polling only; no MSI/MSI-X interrupt completion path yet.
- One command in flight per controller and a single shared DMA bounce page. This prioritizes correctness over throughput and makes concurrent block calls serialize.
- One logical block per NVMe command, including for larger block requests. Larger requests are split into individual commands.
- DMA allocations currently require identity-mapped, below-4-GiB physical pages. The driver therefore does not yet support controllers that require DMA addresses above 4 GiB.
- No namespace hotplug/rescan, power-state management, multipath, or controller reset recovery.
- No verified filesystem write-safety claim: block writes are exposed at the device layer only.

## Verification

The CI build must pass before merge. End-to-end verification should boot QEMU with an NVMe controller and scratch namespace, then check serial markers for controller and namespace discovery, read a known pattern, write/read it back, flush, and reject out-of-range requests. Do not use the OS's boot disk as a destructive test target.
