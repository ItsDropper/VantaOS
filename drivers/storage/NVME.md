# NVMe driver

The NVMe driver discovers PCI NVMe controllers and registers supported namespaces as generic block devices. FAT32 consumes the same block-device interface for NVMe and ATA; controller-specific commands stay inside each driver.

## Implemented data path

- PCI class 01h / subclass 08h / programming interface 02h discovery.
- Admin and I/O queue depth is selected from CAP.MQES (capped at 64 entries), rather than rejecting controllers that support fewer than 64 entries; batches never exceed the available ring capacity.
- BAR0 MMIO mapping sized from CAP.DSTRD, capability validation, controller ready handshakes, and Admin Submission/Completion Queues.
- Identify Controller and active-namespace discovery; unsupported metadata formats and logical block sizes outside 512–4096 bytes are rejected.
- Block-device read/write/flush, 64-bit LBA handling, range validation, and bounded polling.
- Direct DMA from validated, mapped caller buffers when every page is present, addressable below 4 GiB, and writable when the controller will write into it; falls back to preallocated DMA buffers when direct mapping is unsafe or unavailable.
- PRP1/PRP2 and PRP-list construction for multi-page transfers. Each I/O command can transfer up to 16 pages, capped further by Identify Controller MDTS.
- Up to eight I/O commands are submitted per batch and the submission doorbell is rung once per batch. Completion queue entries are matched by command ID, so commands may complete out of submission order.
- DMA buffers are preallocated per queue slot to avoid per-I/O allocation. The QEMU driver self-test exercises multi-page transfers, write/readback, flush, out-of-range rejection, and restoration of the scratch range.

## Filesystem integration

- ATA and NVMe expose block_device_t objects through block_device_register(). Filesystem code must not call controller APIs directly.
- FAT32 requires 512-byte logical sectors and writable devices. It validates MBR partition extents and FAT32 volume geometry against the selected device's reported capacity, using overflow-safe intermediate arithmetic.
- FAT32 probes all registered eligible devices. It mounts only when exactly one device contains a valid FAT32 volume. If multiple devices contain valid volumes, mounting is refused rather than choosing by registry order. A volume can be selected deterministically by making it the only eligible FAT32 volume; explicit boot-volume identity is not implemented yet.
- If no persistent volume is available, VantaOS retains its in-memory namespace. Kernel startup initializes ATA and NVMe before filesystem initialization; filesystem initialization does not reinitialize drivers.
- The NVMe-only QEMU filesystem test attaches a disposable FAT32 image only as NVMe, asserts VANTAOS_FAT32_BACKEND_NVME_OK, exercises real filesystem mutations and multi-cluster I/O, reboots with the same image, and verifies the persisted file. The ATA regression matrix separately requires VANTAOS_FAT32_BACKEND_ATA_OK.

## Remaining production work

This is a substantial performance improvement, but it is **not yet a complete production-grade storage stack**.

- Completion is still polled; MSI/MSI-X interrupts and interrupt-driven wait queues are not implemented.
- The DMA layer is identity-mapped, below-4-GiB, cache-coherent x86 only. No IOMMU mapping, DMA mask negotiation, or high-memory support.
- The direct-DMA fast path depends on the current x86 identity-mapped/below-4-GiB DMA contract. It falls back to preallocated DMA buffers and copies when a caller buffer cannot be safely mapped.
- A single I/O queue is used; per-CPU/multi-queue scaling, queue affinity, and scheduler-aware blocking are not implemented.
- Error handling fails closed on timeout, controller-fatal status, or queue corruption. Controller reset/reinitialization, namespace hotplug/rescan, power management, and telemetry are not implemented.
- FAT32 operations flush each sector write for conservative durability; this is correct but expensive. Filesystem-level crash consistency and multi-sector transactions are not implemented.

## Running the integration tests

Run the repository GitHub Actions workflow at .github/workflows/build.yml or manually reproduce the NVMe-only FAT32 filesystem integration and reboot persistence step. It copies the disposable CI image before attaching it to QEMU, does not use a real disk, requires explicit NVMe backend and filesystem success markers, and checks /VNTTEST.TXT after reboot using mcopy.
