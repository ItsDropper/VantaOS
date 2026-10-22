# GPU driver bring-up status

VantaOS has separate NVX (NVIDIA), Intel, and AMD driver backends over a shared
PCI identity/lifecycle layer. These are bring-up stages, not yet complete
accelerated drivers.

## Implemented in this stage

- Read-only PCI BAR inventory for type-0 endpoints and type-1 bridges.
- Correct handling of 64-bit BAR pairs, assigned versus unassigned addresses,
  I/O versus memory BARs, and the prefetchable attribute.
- Reject malformed/reserved BAR encodings and inconsistent config-space results.
- Intel and AMD descriptor queries now report PCI command state and the assigned
  MMIO resources available for subsequent mapping.
- Resource inspection does not resize BARs, change PCI command bits, map device
  memory, enable bus mastering, or advertise acceleration.
- Strict-warning host tests cover 64-bit pairing, mixed I/O/MMIO resources,
  malformed BAR rejection, and fail-closed vendor capability reporting.

## Shared runtime logic added in this branch

- Bounds-checked 32-bit register reads/writes, masked updates, bounded status polling, bitfield encoding/decoding, and ordered write/verify sequences.
- Transactional command-ring reservations with atomic packet publication, wraparound accounting, back-pressure, monotonically increasing software fences, and a fail-closed hardware-tail callback contract. The platform callback must provide the required memory ordering and device-specific doorbell operation.
- A staged bring-up executor that only accumulates capabilities after verification, unwinds completed stages in reverse order on failure, and clears all published capabilities if rollback is incomplete.
- Host tests exercise failure injection and teardown paths. These abstractions are not a substitute for vendor-specific command encodings, DMA-coherent allocations, real MMIO mappings, firmware validation, or physical-device testing.

## Current status by vendor

### NVIDIA / NVX

The GA10x path includes PCI matching, BAR validation/mapping, PMC probing,
firmware bundle parsing, GSP boot-memory staging, WPR metadata planning, and a
SEC2 signed Booter Load sequence with synthetic-MMIO tests. The secure boot path
has not been validated on physical hardware.

Next gates: authoritative WPR input discovery/policy, real DMA/coherency
validation, GSP RISC-V launch/readiness, GSP-RM RPC initialization, then channel
and command submission. Hardware acceleration must remain unpublished until
each relevant path is verified.

### Intel

PCI family classification and read-only BAR discovery are implemented.
Hardware initialization remains deliberately disabled.

Next gates: select a specific supported generation first; validate its
GTTMMADR/register aperture and forcewake requirements; add generation-specific
reset and GGTT setup; load and authenticate the correct GuC/HuC firmware where
required; initialize one engine and verify completion before exposing 2D/3D
capabilities. Do not generalize register offsets across Gen9 and Xe families.

### AMD

PCI family classification and read-only BAR discovery are implemented.
Hardware initialization remains deliberately disabled.

Next gates: start with one explicitly supported ASIC; validate its MMIO
aperture and reset sequence; implement PSP firmware loading/authentication,
GMC/VM setup, ring buffers and fences; initialize one graphics or DMA engine and
verify completion before exposing capabilities. Do not treat all GCN/RDNA
families as register-compatible.

## Verification boundary

A PCI identity, assigned BAR, successful MMIO mapping, or synthetic register
test does not prove that a GPU is initialized. No Intel or AMD 3D acceleration
is currently claimed, and NVX's secure boot sequence is not equivalent to a
working graphics command path. The next implementation should target one
specific GPU generation end-to-end instead of adding placeholder capability
flags for every family.
