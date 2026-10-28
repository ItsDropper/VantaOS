# AHCI/SATA bring-up plan

VantaOS currently has legacy IDE-compatible ATA PIO and NVMe code. The
`ahci_prdt_build()` helper is the first isolated AHCI primitive; it is not a
SATA driver and must not be reported as device support.

## Implemented primitive

- Encodes the 16-byte AHCI PRDT entry layout.
- Splits a contiguous DMA address range into entries of at most 4 MiB each.
- Validates nonzero length, DWORD-aligned DMA address, 64-bit end-address overflow,
  output capacity, and the PRDT byte-count encoding.
- Sets IOC only on the final entry when requested.
- Leaves the entry array unchanged when validation fails.

The helper assumes its caller already owns a device-visible DMA mapping. It does
not allocate DMA memory, satisfy command-list/FIS/command-table alignment,
perform cache synchronization, or transfer buffer ownership to the HBA.

## Required next stages

1. Add AHCI PCI class/subclass discovery and validate BAR5 as an MMIO resource.
2. Implement a reset/enable state machine with bounded waits and fail-closed
   recovery; do not reset an active controller without an explicit ownership
   contract.
3. Define aligned command-list, received-FIS, command-table, and PRDT allocations
   using VantaOS's DMA allocator and physical-address contract; the PRDT builder
   now rejects DMA addresses not aligned to a four-byte boundary.
4. Implement IDENTIFY DEVICE and bounded READ DMA EXT / WRITE DMA EXT commands,
   with command-slot ownership, timeout handling, status/error capture, and
   cache/ordering rules.
5. Integrate only after disposable QEMU disk-image tests cover timeouts,
   controller errors, malformed capacities, DMA boundaries, and readback.
6. Add physical hardware validation only after emulator tests pass.

Do not enable PCI bus mastering or issue writes until DMA mappings and controller
ownership are demonstrably safe. Test against disposable images, never a host
system disk.
