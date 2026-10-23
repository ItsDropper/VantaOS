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
- Transactional command-ring reservations with atomic packet publication, wraparound accounting, back-pressure, monotonically increasing software fences, vendor-validator gating before submission, and a fail-closed hardware-tail callback contract. The platform callback must provide the required memory ordering and device-specific doorbell operation.
- A staged bring-up executor that only accumulates capabilities after verification, unwinds completed stages in reverse order on failure, and clears all published capabilities if rollback is incomplete.
- Host tests exercise failure injection and teardown paths. These abstractions are not a substitute for vendor-specific command encodings, DMA-coherent allocations, real MMIO mappings, firmware validation, or physical-device testing.

## First generation-scoped targets

The follow-up implementation narrows the first targets to Intel Skylake Gen9 GT2 (PCI IDs `8086:1912` and `8086:1916`) and AMD Polaris 10/20 (PCI IDs `1002:67DF` and `1002:67C0`). The rationale is architectural maturity, deployment age, existing public documentation and a tractable first ASIC family rather than chasing the newest hardware. This is a scoped bring-up target, not a prevalence ranking: this repository does not contain reliable current installed-base statistics, so no claim is made that these are the most-used GPUs today.

Generation-specific read-only diagnostics now sample Intel Gen9 render-ring HEAD/TAIL/START/CTL/ACTHD/MODE/RESET_CTL and Polaris GCN4 GRBM/SRBM/CP status registers through the shared bounds-checked MMIO abstraction. Generation-specific 4 KiB leaf-PTE encoders are also implemented: Intel Gen9 encodes present/write and the legacy PAT cache policy, while Polaris encodes valid/system/snooped/read/write/execute permissions. Both reject unaligned or out-of-range physical addresses and invalid permissions; strict host tests verify exact bit patterns. Snapshots publish only after all reads succeed; Intel ring size and pointer offsets are checked before publication. These paths never request reset, write registers, or submit work. Strict-warning host tests cover valid snapshots, out-of-range mappings, malformed ring geometry, and null arguments.\n\nThe new profile modules perform exact-ID matching and read-only BAR preflight. Conservative generation-specific command-stream preflight is now available: Intel Gen9 accepts only MI_NOOP and a final MI_BATCH_BUFFER_END, while Polaris accepts only PM4 NOP packets. The strict host tests reject truncated packets, trailing data, unknown/privileged command classes and streams beyond configured bounds. These validators are not hardware command submission and intentionally cannot execute workloads. Intel requires an assigned, 64-bit, page-aligned BAR0 candidate for GTTMMADR and marks GuC/HuC firmware as required. AMD requires an assigned, page-aligned memory BAR2 candidate and records the PSP, SDMA and graphics firmware dependencies. Both profiles keep `acceleration_ready` false unconditionally. The host tests exercise accepted/rejected IDs, missing BARs, malformed alignment and argument errors; they do not emulate GPU execution.

These profile checks intentionally stop before touching hardware. They do not yet implement Intel forcewake/reset/GGTT PTE programming/GuC authentication and engine submission, or AMD PSP firmware authentication/GMC page-table programming/SDMA or graphics ring execution. The current command validators are deliberately NOP-only; they are a fail-closed parser boundary, not evidence of rendering or compute support. Do not advertise acceleration until those generation-specific stages and a hardware completion/fence test are implemented and verified.

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

The first scoped profile is Skylake Gen9 GT2 (`8086:1912`, `8086:1916`). Exact-ID matching and read-only GTTMMADR preflight are implemented; hardware initialization remains deliberately disabled. Next gates: authoritative register definitions and forcewake/reset handling, GGTT PTE construction and invalidation, authenticated GuC/HuC loading, then engine submission and fence completion.

### AMD

PCI family classification and read-only BAR discovery are implemented.
Hardware initialization remains deliberately disabled.

The first scoped profile is Polaris 10/20 (`1002:67DF`, `1002:67C0`). Exact-ID matching and read-only BAR2 preflight are implemented; hardware initialization remains deliberately disabled. Next gates: validated ASIC-specific register definitions, PSP firmware authentication/loading, GMC page-table format and invalidation, SDMA/graphics packet submission, interrupt/fence completion and recovery. Do not treat all GCN/RDNA families as register-compatible.

## Verification boundary

A PCI identity, assigned BAR, successful MMIO mapping, or synthetic register
test does not prove that a GPU is initialized. No Intel or AMD 3D acceleration
is currently claimed, and NVX's secure boot sequence is not equivalent to a
working graphics command path. The next implementation should target one
specific GPU generation end-to-end instead of adding placeholder capability
flags for every family.
