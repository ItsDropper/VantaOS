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
| GSP-RM RPC and RM objects | Protocol, core/channel/CTXDMA and system-memory object lifecycle primitives exist; host-tested | Successful first real RPC and object create/control/free round-trip on GA106 |
| GPU memory / GPUVM | Software reservation, DMA/page-table primitives and RM system-memory allocation lifecycle exist; host-tested | CPU mapping, CTXDMA/NVOS46 mapping, hardware-correct page tables, translation controls, TLB invalidation, real VRAM accounting and proven mapping visibility |
| GPFIFO / channel / synchronization | Layout and validation primitives exist | Hardware channel provisioning, real mapped GPFIFO/USERD/fence resources and GPU-produced completion |
| Copy engine | Command builder and bounded GPFIFO submit/wait transaction implemented, host-tested; hardware provisioning unverified | A real CE submission copying known bytes between distinct GPU-visible buffers, followed by independent CPU verification |
| Graphics engine | No verified working 3D path | Engine context, graphics channel, shader/program execution, render target, completion and CPU-verified output |
| Display/presentation | Software display path exists independently | Explicit synchronization and scanout integration; GPU rendering alone does not prove page flipping or VSync |
| Vulkan or other game API | Not established | Kernel/userspace ABI, memory/sync semantics, shader compiler/codegen, pipelines/descriptors, presentation and conformance tests |

## Hardware-ABI audit against NVIDIA's public headers

Evidence level: **audited, host-tested.** The hardware-facing constants and encodings below were checked against NVIDIA's `open-gpu-kernel-modules` headers (git `e6a2e01`, `src/common/sdk/nvidia/inc` and `src/nvidia/generated/g_gpu_class_list.c`) by compiling NVIDIA's own definitions next to NVX's. [`scripts/test_nvx_abi_conformance.c`](../../../scripts/test_nvx_abi_conformance.c) pins the results in CI. This shows NVX matches the published ABI. It does **not** show that a GA106 accepted any of it.

| Item | Result |
| --- | --- |
| `NV_CHANNEL_ALLOC_PARAMS` | Identical: 376 bytes and every field offset match (x86-64). |
| RM class IDs (root, device, `NV01_MEMORY_VIRTUAL`, Turing/Ampere GPFIFO A) and engine type `COPY0` | Match. |
| `NVA06F_CTRL_CMD_GPFIFO_SCHEDULE` / `_BIND` | Match (`0xA06F0103` / `0xA06F0104`). |
| `NVC5B5` copy method offsets and `LAUNCH_DMA` bit fields | Match. `NVC6B5` and `NVC7B5` use identical offsets and fields for everything the builder emits. |
| GPFIFO entry encoding | **Was wrong; fixed.** See below. |
| Copy-engine class and `SET_OBJECT` payload | **Were wrong; fixed.** See below. |

### Defects found and fixed

1. **GPFIFO `GP_ENTRY0` was address/4.** `GP_ENTRY0.GET` is bits 31:2 and holds `VA[31:2]` in place, so the dword is the low address word with bits 1:0 clear. NVX stored `(VA >> 2)`, so the GPU would have fetched its command buffer from one quarter of the intended address. The old test computed its expected value with the same formula as the implementation, so both agreed and both were wrong. The test now uses literal vectors derived independently from NVIDIA's field definitions, and it fails against the old encoder.
2. **Copy-engine class was Turing's, and `SET_OBJECT` was given a handle.** `NVX_GSP_CE_AMPERE_DMA_COPY_CLASS` was `0xC5B5` (`TURING_DMA_COPY_A`). NVIDIA's class list exposes `AMPERE_DMA_COPY_B` (`0xC7B5`) on GA106 CE0 to CE4. Separately, `SET_OBJECT` takes the class ID in bits 15:0, but the builder emitted whatever "object" it was given and its test passed an RM-handle-like value. The builder now takes `copy_class`, accepts only the known CE classes, and rejects anything else. The constants are `NVX_GSP_CE_{TURING_DMA_COPY_A,AMPERE_DMA_COPY_A,AMPERE_DMA_COPY_B}_CLASS` plus `NVX_GSP_CE_GA10X_COPY_CLASS`.

Both defects would only have appeared on real hardware as a channel that silently never executed. Neither can be found by synthetic-MMIO tests, which is why the pins exist.

### Not yet audited

USERD `GP_GET`/`GP_PUT` offsets and the usermode doorbell (NVX takes borrowed pointers and does not hardcode them), `NVOS46`/`NVOS47` map-memory layouts, the RM memory-allocation parameter structs, and the GSP message-queue and init-argument layouts, which depend on the exact firmware release. Audit these against the headers matching the firmware you load before trusting them.

Do not add a per-SKU source file merely to record an identity. Shared GA10x/Ampere policy belongs in common code unless a documented hardware difference requires a backend.

## Current software execution milestone

`nvx_gsp_ce_copy_submit_and_wait()` now composes the existing Ampere CE
pushbuffer builder with GPFIFO publication, USERD fetch-consumption polling,
and a separate GPU-written completion semaphore check. It rejects a completion
word that already contains the requested value, preserves the submitted PUT
index when a wait times out, and reports the lower-level GPFIFO result.

Strict-warning CI host tests cover the success path, stale completion,
invalid copy ranges, queue-full rejection, and timeout ownership semantics.
These tests use a simulated producer/consumer and completion callback. They do
not establish that a physical GA106 accepts the class, pushbuffer, semaphore,
channel mappings, or command packet. The caller must still provide a real
RM-created CE object and channel, valid GPUVA mappings, cache/DMA synchronization,
and safe resource retention until completion or proven quiescence.

## Copy-channel lifecycle software milestone

NVX now has a reusable RM-backed Ampere copy-channel transaction in
`include/nvx_gsp_channel.h` and `drivers/gpu/nvx/gsp/nvx_gsp_channel.c`.
It allocates the GPFIFO channel, binds it to `COPY0`, schedules it, and
tears it down in the reverse safe order. Timed-out stages retain their RPC
handle and explicit stage; callers must recover the operation before reusing
response storage or changing channel ownership. A failed bind/schedule rolls
back the channel, and failed cleanup preserves the allocated state for retry.

The host regression tests cover successful provisioning/teardown, allocation
and schedule timeout recovery, failed bind rollback, failed disable refusal,
free timeout recovery, handle alias rejection, and corrupted-state rejection.
This is software lifecycle verification only: no physical GSP/RM channel has
yet been created on a GA106, and the current channel memory descriptors still
must be backed by real RM-visible mappings before submission.

## Per-object GPU memory access policy

The NVX memory-object allocator now accepts explicit read or read/write
access. Read-only allocations are encoded through the GPUVM PTE read-only
bit; the legacy allocation entry point remains read/write for compatibility.
Write-only mappings are rejected because the current V2 PTE policy does not
represent a safe write-only mapping. Memory metadata reports the effective
access mode, and validation rejects corrupt access state.

Host tests cover invalid write-only requests and read-only allocation
ownership. Hardware enforcement and actual page-fault behavior remain
unverified on GA106.

Submission validation also checks each relocation's requested read/write access against the target object's GPUVM permissions. A command cannot request a writable relocation to a read-only allocation; the validator reports a distinct target-access failure before a batch lease is acquired. This is host-tested and does not replace hardware-side protection verification.

## Fence-gated submission leases

The submission validator now has a lease layer in
`include/nvx_submit.h` and `drivers/gpu/nvx/execution/nvx_submit.c`.
It pins the command buffer and each unique relocation target before the caller
hands the batch to an engine. Duplicate target references are pinned once.
Release proceeds in reverse order and preserves its cursor after an unpin
failure, so a retry does not double-release already completed pins.

`nvx_submit_release_after_fence()` checks the CPU-side NVX timeline before
releasing a lease: pending, stale, and failed fences retain all pins. The
timeline itself is not a hardware semaphore; only a trusted completion path
may signal it. Device loss alone is not proof that DMA has stopped. The caller
must separately establish engine quiescence before using the lower-level
release API for recovery.

Strict-warning host tests cover duplicate-target handling, pending-fence
retention, retry after partial release failure, partial-acquire rollback, and
device-loss retention. No physical GPU completion source is connected yet.

## GPU memory lifetime pinning milestone

The NVX GPU memory-object manager now supports explicit per-object lifetime
pins. Submission code can pin a memory handle before handing its GPUVA to an
engine and unpin only after a completion fence or verified engine quiescence.
Release refuses to unmap/free a pinned object, and pin-count overflow or
underflow is rejected. This closes a class of use-after-free hazards in which
a command buffer, source, destination or semaphore could otherwise be released
while hardware still references it.

Host tests cover multiple pins, release exclusion, balanced unpin, underflow,
and overflow. The API is a lifetime primitive; it does not itself submit work,
observe a GPU fence, or prove quiescence. Callers must enforce that contract.

## RM system-memory object lifecycle

NVX now has a typed owner for NVIDIA's public `NV01_MEMORY_SYSTEM` class
(`0x3e`) using the `NV_MEMORY_ALLOCATION_PARAMS` payload from the public
64-bit `nvos.h` ABI. The structure is pinned by size and field-offset
assertions, and the class/parameter pairing was checked against NVIDIA's
`open-gpu-kernel-modules` source. This deliberately does not use
`NV01_MEMORY_SYSTEM_DYNAMIC` (`0x70`): NVIDIA's own deprecated allocation
path documents that this class does not allow DMA mappings.

The wrapper submits the RM allocation, waits for its completion, decodes the
returned size and inclusive limit from the complete allocation response, and
tracks free/recovery ownership. If RM reports allocation success but the
returned parameter payload is truncated or malformed, NVX retains the remote
object in a destroyable state rather than discarding the handle. Timeouts and
transport errors preserve the in-flight RPC handle; failed frees preserve the
created memory object. The caller supplies version-matched NVOS32 flags and
attributes rather than relying on guessed placement or cache-policy values.

Strict-warning host tests cover ABI offsets, successful allocation/free,
invalid alignment, timeout and transport recovery, malformed-result ownership,
remote free failure, and retryable local RPC-handle release.

**This is an RM object lifecycle milestone, not yet CPU/GPU memory visibility.**
The API does not yet establish a CPU mapping, prove the returned allocation is
usable by CE, or perform NVOS46 mapping. The next step is to add and audit the
correct RM CPU-map path (where required), then create a CTXDMA object over this
memory, map it into GPUVA, and use that memory for channel buffers and a real
copy test. No GA106 hardware allocation has been verified.

## RM context-DMA object lifecycle

NVX now has a typed wrapper for NVIDIA's public `NV01_CONTEXT_DMA` class
(`0x2`). It creates a context-DMA object over a caller-owned RM memory
object using the documented 32-byte `NV_CONTEXT_DMA_ALLOCATION_PARAMS`
layout, then frees it without releasing the underlying memory object. The
ABI size and 64-bit field offsets are compile-time asserted.

Allocation/free use explicit submit, wait, and local-handle release stages.
Timeouts and transport errors retain the operation handle; completed RPC
release failures can be retried without submitting a duplicate allocation or
free. Failed remote frees preserve the created object. The caller must keep
the parent memory object alive until context-DMA teardown completes.

Host tests cover the class ID and parameter layout, parent/child handles,
allocation and free, timeout recovery, transport errors, remote failure,
retryable handle release, handle aliasing, and in-flight buffer overlap.
This still does not allocate the backing RM memory object or prove a real
CTXDMA object can be created on GA106.

## RM DMA mapping lifecycle software milestone

`include/nvx_gsp_dma_map.h` and
`drivers/gpu/nvx/gsp/nvx_gsp_dma_map.c` add a reusable owner for the existing
NVOS46/NVOS47 map/unmap RPCs. It records the request, returned DMA offset,
in-flight operation and completion stage. Timeouts retain the RPC handle;
recovery can finish the operation without submitting a duplicate map or
unmap. A local RPC-handle release failure is separately retryable. Failed
unmaps retain the mapped state so callers cannot accidentally treat memory as
detached.

The NVOS46/NVOS47 wire layout is now audited against NVIDIA's public\n[`nvos.h`](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/e6a2e01579fbbd1ca2ed43fef2ee2589e9bd8bd9/src/common/sdk/nvidia/inc/nvos.h). NVX's map input fields occupy 56 bytes with the aligned dmaOffset at byte 48; the serialized request is 64 bytes including the zeroed status/padding tail, and the response status is at byte 56. The unmap input fields occupy 40 bytes with dmaOffset at byte 24 and size at byte 32; its serialized request is 48 bytes and response status is at byte 40. Compile-time field-offset assertions and independent response-status tests pin these values.\n\nThe NVOS47 unmap request uses its own flag field and currently defaults to immediate invalidation; NVOS46 map flags are never forwarded into the different NVOS47 bit layout.

The NVOS46 request preserves its input dmaOffset: NVIDIA's ABI makes that field an input when fixed-offset mapping is requested (and for DMA objects that are not CTXDMA handles), otherwise it is an output. A returned offset of zero is valid and remains unmap-able. Host tests cover both the fixed-input and zero-result cases.

The host test covers successful map/unmap, map and unmap timeout recovery,
response decode failure, request-range overflow, retryable RPC-handle release,
and the guarantee that recovery does not submit a duplicate operation.
This is lifecycle verification only. It does not audit the NVOS46/NVOS47 wire
layout, create the underlying RM memory/DMA objects, or prove that the
returned offset is usable on GA106. The RM system-memory object ABI and NVOS46/NVOS47 wire layouts are now
implemented and host-tested, but the first real allocation, CPU mapping, and
actual GSP-RM DMA mapping round-trip remain explicit hardware-readiness gates.

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
3. Create an NV01_MEMORY_SYSTEM object, validate its returned size/limit, and establish a CPU mapping with the version-matched RM API.
4. Create CTXDMA over that memory, perform NVOS46 GPU mapping, and validate actual GPUVM mapping and invalidation behavior.
5. Back GPFIFO, USERD, fence and copy buffers with those mappings; prove one CPU-verified copy-engine operation end to end.
6. Only then begin graphics-engine context/shader/render-target work and later the userspace graphics ABI/Vulkan path.

Every change should identify the exact evidence level, cite public register/ABI references in the reverse-engineering notebook, and include strict-warning host tests for independently testable behavior.
