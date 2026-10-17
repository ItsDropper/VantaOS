# Storage driver architecture

The storage subsystem has three layers.

- PCI storage-driver registry: matches PCI class/subclass/programming-interface and optional vendor/device IDs. A probe callback claims a controller only after successful initialization.
- Block-device registry: uniform sector-based reads, writes, and flushes. It validates request ranges and read-only policy before dispatching hardware-specific operations.
- DMA page API: zeroed, page-aligned, below-4-GiB coherent pages for controller queues and PRP lists. Each allocation is exactly one physically contiguous page; adjacent allocations are not assumed contiguous.

## Driver rules

1. Register block devices only after the controller is initialized and capacity/sector size are known.
2. Enable PCI bus mastering only after DMA memory and controller state are ready.
3. Bound every hardware wait and report timeout/completion errors as I/O failures.
4. Validate LBA/count without overflow. Never DMA directly from user pointers.
5. Keep hardware serialization in drivers; the generic registry lock protects registry metadata only.
6. Return UNSUPPORTED when flush is unavailable.
7. Stop DMA before freeing DMA pages; unregister devices before freeing their storage.
8. Probe callbacks run without the registry lock and may register block devices.

## NVMe target

Bind PCI class 01h, subclass 08h, programming interface 02h. Read the controller BAR, map MMIO, allocate admin/I/O queue pages through the DMA API, identify namespaces, and register each usable namespace as a block device. Use 64-bit LBAs. Initial bounded polling is acceptable while MSI/MSI-X routing is brought up, but waits must time out.
