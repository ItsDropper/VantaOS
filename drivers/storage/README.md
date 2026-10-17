# Storage driver architecture

The storage subsystem has three layers.

- PCI storage-driver registry: matches PCI class/subclass/programming-interface and optional vendor/device IDs. A probe callback claims a controller only after successful initialization.
- Block-device registry: uniform sector-based reads, writes, and flushes. It validates request ranges and read-only policy before dispatching hardware-specific operations.
- DMA page API: zeroed, page-aligned, below-4-GiB coherent pages for controller queues and PRP lists. Each allocation is exactly one physically contiguous page; adjacent allocations are not assumed contiguous.
- MMIO region mapper: maps physical BAR ranges into unused pages of the reserved uncached kernel MMIO window, supports non-page-aligned ranges, and tracks exact mappings for safe unmap.

## Driver rules

1. Register block devices only after the controller is initialized and capacity/sector size are known.
2. Use mmio_map_region() for controller BARs; never reuse hard-coded virtual addresses or alias APIC/IOAPIC mappings.
3. Enable PCI bus mastering only after DMA memory and controller state are ready.
4. Bound every hardware wait and report timeout/completion errors as I/O failures.
5. Validate LBA/count without overflow. Never DMA directly from user pointers.
6. Keep hardware serialization in drivers; the generic registry lock protects registry metadata only.
7. Return UNSUPPORTED when flush is unavailable.
8. Stop DMA before freeing DMA pages; unregister devices before freeing their storage.
9. Probe callbacks run without the registry lock and may register block devices.

## Block-device consumers and selection

ATA currently probes the legacy channels and registers the first supported LBA28 disk as ata0, using its IDENTIFY-reported sector count and 512-byte sectors; additional ATA disks are not enumerated yet. NVMe registers each supported namespace with a stable nvme... name and its Identify-reported capacity/sector size. Both drivers implement generic read/write/flush operations; callers should not use ATA or NVMe entry points directly for filesystem I/O.

FAT32 uses the selected block_device_t for every partition-relative read and write. It currently requires 512-byte sectors and a writable volume. It checks partition and BPB bounds against the selected device's capacity. Device selection is intentionally conservative: probe all eligible devices and mount only if exactly one contains a valid FAT32 volume. If two devices contain valid FAT32 volumes, FAT32 refuses to mount either rather than modifying an arbitrary disk. Explicit boot-volume identity and partition GUID selection are future work.

## NVMe target

Bind PCI class 01h, subclass 08h, programming interface 02h. Read the controller BAR, map MMIO, allocate admin/I/O queue pages through the DMA API, identify namespaces, and register each usable namespace as a block device. Use 64-bit LBAs. Initial bounded polling is acceptable while MSI/MSI-X routing is brought up, but waits must time out.
