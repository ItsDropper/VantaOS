# AHCI/SATA bring-up plan

VantaOS has legacy IDE-compatible ATA PIO and NVMe code. The PRDT builder,
ABAR validator, and read-only PCI discovery stage are AHCI groundwork, not a
SATA driver and must not be reported as device support.

## Implemented primitives

- Encodes the 16-byte AHCI PRDT entry layout.
- Splits a contiguous DMA address range into entries of at most 4 MiB each.
- Validates nonzero length, DWORD-aligned DMA address, 64-bit end-address overflow,
  output capacity, and the PRDT byte-count encoding.
- Sets IOC only on the final entry when requested.
- `ahci_pci_abar_valid()` rejects I/O BARs, zero bases, undersized/non-power-of-two apertures, misaligned ranges, and address overflow. It validates caller-supplied metadata only; it does not probe PCI or measure BAR size.
- `ahci_initialize()` scans AHCI-class PCI functions and inspects CAP/PI/version only when PCI I/O decode, memory decode, and bus mastering are disabled. It measures BAR5 only under that safe condition, temporarily enables memory decode for a read-only register probe, unmaps the range, and restores the original command register.
- Host regression coverage exercises the normal discovery/restore path, refuses a function with bus mastering already enabled before touching BARs, and verifies cleanup after invalid CAP or GHC register reads.
- Leaves the entry array unchanged when validation fails.

The helper assumes its caller already owns a device-visible DMA mapping. It does
not allocate DMA memory, satisfy command-list/FIS/command-table alignment,
perform cache synchronization, or transfer buffer ownership to the HBA.

## Required next stages

1. Extend discovery to handle firmware-active controllers through an explicit safe handoff/resource contract. Current discovery intentionally skips functions with I/O decode, memory decode, or bus mastering already enabled.
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
