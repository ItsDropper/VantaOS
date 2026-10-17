# NVMe driver

The NVMe driver is a kernel-mode PCI driver that exposes supported NVMe namespaces through the generic block-device registry.

## Implemented data path

- PCI class 01h / subclass 08h / programming interface 02h discovery.
- BAR0 MMIO mapping sized from CAP.DSTRD, capability validation, controller ready handshakes, and Admin Submission/Completion Queues.
- Identify Controller and active-namespace discovery; unsupported metadata formats and logical block sizes outside 512–4096 bytes are rejected.
- Block-device read/write/flush, 64-bit LBA handling, range validation, and bounded polling.
- Direct DMA from validated, mapped caller buffers when every page is present, addressable below 4 GiB, and writable when the controller will write into it; falls back to preallocated DMA buffers when direct mapping is unsafe or unavailable.
- PRP1/PRP2 and PRP-list construction for multi-page transfers. Each I/O command can transfer up to 16 pages, capped further by Identify Controller MDTS.
- Up to eight I/O commands are submitted per batch and the submission doorbell is rung once per batch. Completion queue entries are matched by command ID, so commands may complete out of submission order.
- DMA buffers are preallocated per queue slot to avoid per-I/O allocation. The QEMU self-test exercises all eight slots, multi-page PRP lists, write/readback, flush, out-of-range rejection, and restoration of the scratch range.
- DMA pages are not released after a controller reset timeout because the device may still own them.

## Remaining production work

This is a substantial performance improvement, but it is **not yet a complete production-grade storage stack**.

- Completion is still polled; MSI/MSI-X interrupts and interrupt-driven wait queues are not implemented.
- The DMA layer is identity-mapped, below-4-GiB, cache-coherent x86 only. No IOMMU mapping, DMA mask negotiation, or high-memory support.
- The direct-DMA fast path depends on the current x86 identity-mapped/below-4-GiB DMA contract. It falls back to preallocated DMA buffers and copies when a caller buffer cannot be safely mapped. Per-controller fallback-buffer memory is approximately 544 KiB for the eight slots at the current 16-page cap, plus queues and Identify data.
- A single I/O queue is used; per-CPU/multi-queue scaling, queue affinity, and scheduler-aware blocking are not implemented.
- Error handling fails closed on timeout, controller-fatal status, or queue corruption. Controller reset/reinitialization, namespace hotplug/rescan, power management, and telemetry are not implemented.
- Filesystem-level crash consistency and write ordering are outside this block driver.

## Verification

CI must pass before merge. The QEMU test uses a disposable NVMe scratch image, not the OS boot disk. It checks namespace discovery, batched multi-page read/write/readback, flush, range validation, and restores the scratch sectors after testing.
