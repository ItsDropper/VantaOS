# RTX 3060 readiness gates for NVX

**Target:** GeForce RTX 3060 12 GB (GA106, Ampere / GA10x).  
**Current evidence level:** source-reviewed and host-tested primitives; physical initialization and GPU execution are unverified.

This document is a release gate, not a claim that the driver is ready. A phase may be marked complete only when its evidence exists on the target hardware or in an emulator that implements the relevant NVIDIA interface. Host tests establish software behavior for synthetic inputs only.

## Evidence vocabulary

- **Implemented:** code exists and is included in the production source manifest.
- **Host-tested:** an automated test exercises the implementation using controlled memory/MMIO or synthetic firmware.
- **Emulated:** the real driver path runs against an emulator that implements the relevant device contract. Generic QEMU PCI/MMIO is not an NVIDIA GPU emulator.
- **Hardware-verified:** the physical RTX 3060 produced a result independently checked by the CPU. Register writes, non-timeout polls, or a reported firmware state alone do not qualify.

## Current subsystem status

| Subsystem | Current state | Evidence still required |
| --- | --- | --- |
| PCI identity and BAR/resource discovery | Implemented, host-tested in parts | Record actual RTX 3060 identity, BARs, command bits and resource ownership from a physical boot |
| GA10x firmware parsing and preflight | Implemented, host-tested | Validate the exact firmware files supplied by a legitimate platform provider; record hashes, versions and verifier policy |
| SEC2 signed Booter Load transaction | Implemented as a hardware-facing sequence with synthetic MMIO tests | Real VBIOS/workspace and framebuffer inputs; correct WPR/FRTS policy; authenticated SEC2 completion on GA106 |
| GSP RISC-V launch and readiness | Partial primitives; end-to-end boot not established | Real firmware launch, stable ready state, mailbox/RPC initialization and version-compatible cached init ABI |
| GSP-RM RPC and RM objects | Protocol/object primitives exist | Successful first real RPC, object create/control/free round-trip and error-path recovery |
| GPU memory / GPUVM | Software reservation, DMA and page-table primitives exist | Hardware-correct page-table format, translation controls, TLB invalidation, real VRAM accounting and proven mapping visibility |
| GPFIFO / channel / synchronization | Layout and validation primitives exist | Hardware channel provisioning, real mapped GPFIFO/USERD/fence resources and GPU-produced completion |
| Copy engine | Command construction exists | A real CE submission copying known bytes between distinct GPU-visible buffers, followed by independent CPU verification |
| Graphics engine | No verified working 3D path | Engine context, graphics channel, shader/program execution, render target, completion and CPU-verified output |
| Display/presentation | Software display path exists independently | Explicit synchronization and scanout integration; GPU rendering alone does not prove page flipping or VSync |
| Vulkan or other game API | Not established | Kernel/userspace ABI, memory/sync semantics, shader compiler/codegen, pipelines/descriptors, presentation and conformance tests |

Do not add a per-SKU source file merely to record an identity. Shared GA10x/Ampere policy belongs in common code unless a documented hardware difference requires a backend.

## Mandatory first hardware experiment

The first hardware milestone should be a bounded copy-engine operation, not a 3D demo. It isolates memory visibility, channel provisioning, submission, completion and recovery before shader execution adds further unknowns.

1. Boot VantaOS on the physical RTX 3060 with acceleration disabled by default.
2. Record PCI vendor/device/subsystem/revision IDs, BAR types/sizes, PCI command register, detected architecture, firmware component hashes/versions, and every startup phase/result.
3. Allocate two independent, page-aligned DMA-backed buffers through the real platform allocator. Record CPU physical addresses and GPU virtual addresses separately; never substitute a CPU pointer for a GPU address.
4. Fill source with a deterministic nontrivial byte pattern and destination with a different sentinel pattern. Flush/synchronize according to the platform DMA coherency contract.
5. Establish the hardware-required GPU mappings and channel resources. Do not enable bus mastering until all ranges and teardown/recovery paths are valid.
6. Submit one minimal, documented GA10x copy-engine operation through a real channel and GPFIFO. Use bounded waits; on timeout, retain any memory that the GPU may still access until reset/quiescence is proven.
7. Synchronize for CPU access and compare every destination byte against the source. Save the mismatch count, first mismatch, submission/completion sequence values, elapsed time, and all relevant error registers.
8. Repeat with zero-length rejection, misalignment, out-of-range addresses, deliberate timeout/fault injection where safely possible, and teardown after both success and failure.
9. Mark the operation **hardware-verified** only if the CPU observes the GPU-written result on physical hardware and the log identifies the exact device and firmware.

A synthetic MMIO test can validate ordering and failure handling, but cannot pass this hardware gate.

## Stop conditions

Fail closed and keep acceleration disabled if any of the following is unresolved:

- firmware authenticity/version compatibility or the correct WPR/FRTS inputs;
- whether an engine is idle before reclaiming device-owned DMA memory;
- GPU physical-address width, page-table encoding, mapping permissions or TLB invalidation semantics;
- command packet semantics or channel resource layout;
- timeout recovery and safe memory lifetime after a partially completed operation.

Do not mark a milestone complete from PCI detection, firmware parsing, a successful register write, a moving GET pointer, a synthetic completion value, or passing CI.

## Immediate engineering sequence

1. Reconcile the physical firmware-provider contract and the remaining SEC2-to-GSP handoff inputs.
2. Complete a version-matched GSP launch and first GSP-RM RPC on hardware.
3. Validate actual GPUVM mapping and invalidation behavior.
4. Provision a real channel and prove one copy-engine operation end to end.
5. Only then begin graphics-engine context/shader/render-target work.
6. Build the userspace graphics ABI and Vulkan path after execution, synchronization, memory, and presentation contracts are stable.

Every change should identify the exact evidence level, cite public register/ABI references in the reverse-engineering notebook, and include strict-warning host tests for independently testable behavior.
