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

Generation-specific read-only diagnostics now sample Intel Gen9 render-ring HEAD/TAIL/START/CTL/ACTHD/MODE/RESET_CTL and Polaris GCN4 GRBM/SRBM/CP status registers through the shared bounds-checked MMIO abstraction. Intel Gen9 now has a real GGTT PTE path for the 16 MiB GTTMMADR aperture: map/unmap writes aligned 64-bit PTEs at the Gen8+ 8 MiB GTTADR offset, verifies readback, and supports contiguous DMA mappings with rollback and fail-closed uncertain-state reporting. Render forcewake acquire/release uses bounded polling; GGTT invalidation performs the GFX_FLSH_CNTL posting read while forcewake is held. A generic GPU VM adapter exposes this mapping path. Polaris now has a hardware VMID-1 root setup path: the 36-bit, 64 GiB layout uses a 4-entry root and a 4,194,304-entry (32 MiB) contiguous leaf table, programs start/end/base/fault registers, configures two-level walks, performs bounded TLB invalidation, and disables the context before table memory can be freed. Its runtime wires table mapping to generic GPU VM callbacks. A configurable GPU page-table walker now supports Intel Gen9's 4-level 48-bit layout and Polaris GCN4's 2-level, 36-bit VA layout with a four-entry root directory and a 4,194,304-entry contiguous leaf table (32 MiB) for a 64 GiB VM range. It supports lazy child-directory allocation, 4 KiB map/unmap, batched updates, explicit TLB invalidation, fail-closed invalidation errors and quiesce-gated table destruction. Intel Gen9 and Polaris adapters provide their respective PDE/PTE encodings and accept platform-owned DMA-table allocation and hardware TLB-invalidation callbacks; the walker is wired to the generic GPU VM mapping interface. This is a functional page-table layer, not a complete graphics VM: the platform must supply DMA-coherent contiguous table allocation. Polaris VMID-1 root installation and invalidation are implemented, while Intel's GGTT path is available through generic GPU VM callbacks; Intel PPGTT root installation into a valid Gen9 engine context is still missing.\n\nGeneration-specific 4 KiB leaf-PTE encoders are also implemented: Intel Gen9 encodes present/write and the legacy PAT cache policy, while Polaris encodes valid/system/snooped/read/write/execute permissions. Both reject unaligned or out-of-range physical addresses and invalid permissions; strict host tests verify exact bit patterns. Snapshots publish only after all reads succeed; Intel ring size and pointer offsets are checked before publication. The read-only snapshot functions themselves never request reset, write registers, or submit work. Strict-warning host tests cover valid snapshots, out-of-range mappings, malformed ring geometry, and null arguments.\n\nThe generation-specific command modules also provide a restricted fence operation: Intel Gen9 builds a single MI_STORE_DWORD_IMM to one caller-authorized PPGTT address followed by MI_BATCH_BUFFER_END; Polaris builds a single PM4 WRITE_DATA packet to one caller-authorized system-memory fence address. Their specialized validators require the exact target and expected value, preventing these helpers from becoming arbitrary write-command APIs. These packets still require a fully initialized vendor ring and verified device-visible mapping before submission.

A PCI-bound Polaris runtime now maps the register aperture from BAR5 (the 256 KiB non-prefetchable MMIO BAR used by GCN4-class devices), allocates ring/RPTR/fence DMA before enabling bus mastering, configures the GFX ring, executes a restricted PM4 fence packet, and requires device-written fence completion before the AMD GPU lifecycle can start. Shutdown halts the CP, verifies bus-master disable, then releases DMA and unmaps MMIO. The runtime still requires the platform to authenticate/load PSP and GFX firmware first; a caller-provided readiness flag is not itself firmware authentication. AMD BAR2 is a doorbell aperture and is not used as the register MMIO window.
\n\nThe new profile modules perform exact-ID matching and read-only BAR preflight. Conservative generation-specific command-stream preflight is now available: Intel Gen9 accepts only MI_NOOP and a final MI_BATCH_BUFFER_END, while Polaris accepts only PM4 NOP packets. The strict host tests reject truncated packets, trailing data, unknown/privileged command classes and streams beyond configured bounds. These validators are not hardware command submission and intentionally cannot execute workloads. Intel requires an assigned, 64-bit, page-aligned BAR0 candidate for GTTMMADR and marks GuC/HuC firmware as required. AMD requires an assigned, page-aligned 256 KiB register BAR5 candidate and records the PSP, SDMA and graphics firmware dependencies. Both profiles keep `acceleration_ready` false unconditionally. The host tests exercise accepted/rejected IDs, missing BARs, malformed alignment and argument errors; they do not emulate GPU execution.

PCI profile discovery remains read-only. Both Intel Gen9 and AMD Polaris now have optional caller-configured bindings in the generic GPU lifecycle. AMD start requires a restricted PM4 fence packet to be written back by the device; Intel start requires both a matching CSB completion and the expected CPU-visible fence value from a caller-prepared LRCA/context and command ring. Shutdown requires a platform callback to verify engine quiescence before pending state, bus mastering and mappings can be released. The AMD path still assumes the platform has authenticated and loaded PSP/GFX firmware; the Intel path does not create the LRCA/context image, construct its command ring/batch, or load GuC firmware. The generic VM layer and Intel GGTT/PPGTT primitives are not yet a complete address-space integration. These are opt-in minimal execution paths, not a full display/3D/compute driver. Do not advertise general acceleration until firmware loading, context creation, safe recovery, and physical hardware completion tests are implemented and verified.

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

The first scoped profile is Skylake Gen9 GT2 (`8086:1912`, `8086:1916`). The PCI-bound runtime maps the audited 16 MiB GTTMMADR BAR0, owns render forcewake and bus-master state, submits a validated context descriptor through ELSP, and consumes CSB completion events. The generic lifecycle can opt into it only with a caller-prepared GPU-visible LRCA/context, command ring, expected fence value and an engine-quiesce callback. Startup checks both CSB completion and the fence writeback; teardown will not discard pending work unless the callback verifies the engine is quiesced. GuC firmware loading, context-image construction, command-batch generation, reset/recovery and physical-hardware validation remain unfinished.

### AMD

PCI family classification and read-only BAR discovery are implemented.
Hardware initialization remains deliberately disabled.

The first scoped profile is Polaris 10/20 (`1002:67DF`, `1002:67C0`). Exact-ID matching and read-only BAR5 register-aperture preflight are implemented. A configurable lifecycle path maps BAR5, allocates DMA, starts the GFX ring only after platform firmware readiness is asserted, and runs a PM4 fence completion self-test before reporting driver initialization success. PSP/GFX firmware authentication/loading remains a platform prerequisite and is not implemented by this runtime; the caller must not assert readiness until that work has succeeded. The 32 MiB VM leaf-table path remains separately opt-in and is not yet wired into the device runtime's ring address space. SDMA setup, interrupts, reset/recovery, display integration and physical hardware validation remain unfinished. Do not treat all GCN/RDNA families as register-compatible.

## Verification boundary

A PCI identity, assigned BAR, successful MMIO mapping, or synthetic register
test does not prove that a GPU is initialized. No Intel or AMD 3D acceleration
is currently claimed, and NVX's secure boot sequence is not equivalent to a
working graphics command path. The next implementation should target one
specific GPU generation end-to-end instead of adding placeholder capability
flags for every family.
