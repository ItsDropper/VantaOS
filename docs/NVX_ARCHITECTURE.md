# NVX GPU driver architecture

NVX is an independent, unofficial NVIDIA GPU driver project for VantaOS. It is
not produced, endorsed, or supported by NVIDIA. This document distinguishes
implemented PCI identification from hardware functionality.

## Current implementation status

NVX enumerates every function with NVIDIA vendor ID `0x10DE` from VantaOS's
existing PCI registry. Diagnostics include BDF, vendor/device IDs, revision,
class/subclass/programming interface, and subsystem IDs for type-0 headers.
NVIDIA class-`03h` display controllers are treated as GPU candidates; audio and
other auxiliary functions are reported separately.

The identity layer now recognizes a deliberately small, exact PCI-ID table for
RTX 3060 variants already named in VantaOS's PCI database (IDs `2503`, `2504`,
`2507`, `2508`, `2509`, `2544`, and `2548`) as the Ampere generation and
RTX 3060 profile. All other IDs remain `UNKNOWN`. This is device-family
identification only: architecture/implementation register IDs remain unset, and
the table does not imply working initialization, firmware, display, or
acceleration. PCI IDs can be shared or misreported by hardware, so this is not
a substitute for hardware validation.

NVX also reads the PCI command register and reports the current I/O-space,
memory-space, and bus-master bits. This is strictly observational: discovery
does not write the command register or enable bus mastering. The values are
preflight diagnostics, not proof that a BAR is valid or that DMA is safe.

For display-controller candidates, BAR0 is reported as a raw address/type/width.
The PCI subsystem now has a read-only `pci_find_capability()` walker for conventional capabilities (including future MSI/MSI-X discovery); it validates the capability-list status bit, header type, pointer alignment/range, and cycles. It does not enable or configure interrupts. The PCI subsystem also has a serialized `pci_get_bar_size()` path that saves a BAR,
probes its hardware mask, restores the original value, and validates the resulting
power-of-two size and base alignment. It refuses to probe unless I/O decode,
memory decode, and bus mastering are all disabled; it never disables them on its
own. Thus a live GPU with firmware display or DMA state active will still yield
`SIZE_UNKNOWN`, and mapping remains refused. The `nvx_resource` layer consumes a
measured size only when this guarded probe succeeds, then validates width, range
overflow, and `size_t` conversion before using `mmio_map_region()` /
`mmio_unmap_region()`. The identity descriptor still leaves MMIO base and length
unset until a later resource/attach layer has established them. The new `nvx_attach` path consumes a caller-supplied BAR0 resource with trusted size metadata, validates it, maps it, checks PCI memory decoding, probes PMC_BOOT_0/PMC_BOOT_1, and retains the mapping only when the register aperture responds. Failure paths unmap and fail closed; detach unmaps and transitions through QUIESCING to STOPPED. A successful attach is explicitly `PROBED`, not `INITIALIZED`, and publishes no capabilities. The guarded BAR-sizing path can provide size metadata only when decode and bus mastering are already disabled; it deliberately cannot automatically attach to a GPU that is active for firmware display or DMA. The remaining integration blocker is a safe handoff/resource contract for such live devices.

A new `nvx_mmio` layer provides bounded 32-bit volatile register reads and writes over a caller-owned mapping. It rejects null mappings, unaligned base/offset pairs, and accesses that would cross the supplied mapping length; read failures zero the output. The BAR0 probe now uses this common accessor rather than dereferencing register pointers directly. Its test suite covers valid reads/writes and invalid bounds/alignment. The accessors do not establish memory attributes or provide DMA ordering barriers, which remain responsibilities of the platform mapping/synchronization layers.

The NVX lifecycle contract defines explicit states from discovery through
probing, validated resources, initialization, running, quiescing, and stop. It
rejects skipped/out-of-order transitions, prevents capability publication before
initialization, and clears all advertised capabilities on failure. This is a
tested contract only: current PCI discovery does not instantiate or advance a
lifecycle object, and no hardware initialization is implemented yet.

Logs report `DETECTED`, `NOT_ATTEMPTED`, and `UNAVAILABLE` for runtime
functionality. No NVIDIA device is currently claimed as initialized or
accelerated. Physical RTX 3060 validation remains outstanding.

### APIC device interrupt groundwork

The x86-64 interrupt path now reserves a generic vector window (0x40-0xDF,
excluding the existing 0x80 syscall vector) for device MSI/MSI-X handlers.
Dedicated assembly stubs preserve the interrupted register context, pass the
actual vector to a C dispatcher, and issue local-APIC EOI after dispatch. The
registry requires APIC availability before vector allocation, supports
single-owner claim/register/release, drains in-flight callbacks during release,
and documents that the device source must be masked before its handler context
can be reclaimed. Host tests cover vector uniqueness/exhaustion, callback
dispatch, duplicate registration, release, and EOI accounting.

`nvx_irq_route_prepare()` now composes capability validation with OS vector
reservation and handler registration as one rollback-safe transaction. It prefers
MSI-X metadata when present, falls back to MSI only when MSI-X is absent, and
releases a claimed vector if handler registration fails. The route handle records
its chosen mode and capability snapshot; release drains the registered callback.
The handle must be zero-initialized before first use.

The route API now programs a single MSI or MSI-X message only when explicitly
called, after vector claim and handler registration. MSI-X setup masks the
function, maps the validated table, masks the selected entry while writing its
APIC message address/data, then enables the function; teardown masks and disables
delivery before draining callbacks and unmapping the table. MSI setup writes
address/data before setting the enable bit and refuses to take over a capability
already enabled by firmware. PCI config writes use exact-width serialized access
and readback verification.

However, no boot-time NVX path currently calls route enable, and no NVIDIA
interrupt status/acknowledgement handler or engine-specific interrupt decoding
is implemented. This is a tested generic route primitive, not working GPU
interrupt support or proof that the GPU generates valid interrupts.

### Coherent DMA ownership handoff

A DMA visibility API now tracks exclusive CPU/device ownership for a validated
contiguous NVX DMA allocation. It snapshots allocation identity to reject stale
handles, checks every requested byte range, prevents duplicate handoffs, and
uses conservative x86 memory fences at CPU-to-device and device-to-CPU
transitions. Destruction is refused while the device owns the allocation.
Tests cover invalid ranges, double transitions, stale allocation state, and
ownership-safe teardown. This is ordering/ownership infrastructure; callers
must still stop the GPU before reclaiming buffers, and no GPU command engine
currently consumes these buffers.

### Versioned GSP firmware bundle acquisition

A new bundle API acquires the four independently versioned R535-style components:
booter-load, booter-unload, bootloader, and GSP runtime firmware. The caller
supplies the chip directory and exact version rather than having NVX guess a
firmware release. Every component is individually size-bounded and passed
through the platform verification callback before ownership is accepted; the
aggregate bundle is capped at 128 MiB. Missing, malformed, oversized, or
unverified components trigger rollback and release all previously acquired
components. Host tests cover paths, partial failure, verification failure,
size limits, and path traversal rejection.

This addresses the single-blob helper's limitation: modern GSP boot consumes a
coordinated set of version-matched files. This is still acquisition/verification
plumbing, not firmware-container parsing, signature patching, Falcon loading,
GSP boot, or RPC queue initialization.

### GSP RPC transaction tracking

A transaction manager now sits above separate command and response rings. It
reserves bounded result slots before queue publication, publishes queue sequence IDs
before the doorbell callback can expose a request, writes the R535 RPC sequence into each request before publishing the queue
pointer and correlates replies by both sequence and expected function, bounds response copies, treats a nonzero firmware result as failure, reports raw
firmware result fields, rejects stale handles, and transitions outstanding requests to
device-lost on fatal failure. Its lock disables local interrupts in kernel
builds to prevent same-CPU interrupt re-entry. Tests cover pending/completed
requests, stale handles, unmatched messages, function mismatch, undersized
response buffers, slot exhaustion, and device loss.

The manager is not yet called by NVX initialization. Requests exceeding one
16-page queue record are rejected until NVIDIA's continuation-record payload
schema is implemented; no real GSP RPC function schemas or boot-time queue
memory setup exist. It provides the transaction
lifecycle needed by those later steps, not working firmware control by itself.

### GSP RPC ring transport

The driver now implements the NVIDIA MCTP/NVDM/RPC v03.00 queue envelope:
`MCTP` magic, total message length, MCTP 1.0 single-message header, NVIDIA
vendor-defined PCI NVDM header with RM-RPC type 0x25, NVDM payload length,
reserved field, and the 32-byte RPC header. It uses NVIDIA's documented
version 3.00 and signature constants, pending-result sentinels, and echoed RPC
sequence IDs. Queue entries are 4 KiB pages, messages can span up to 16 pages,
the producer/consumer indices are monotonic 32-bit counters (matching NVIDIA
MSGQ 2.0), and all queue slots are usable; publication uses release/acquire
ordering.
The receive path validates every envelope length and protocol discriminator
before consuming a message. There is no invented checksum field in the wire
format. Tests cover full/empty queues, multi-page wraparound, malformed
headers, request/reply sequence correlation, and notification counts.

The implementation is grounded in NVIDIA's own published source rather than
Nouveau: `src/nvidia/arch/nvalloc/common/inc/mctp_format.h`,
`nvdm_format.h`, `src/nvidia/inc/kernel/gpu/gsp/message_queue_priv.h`,
`src/nvidia/generated/g_rpc-message-header.h`, and the GSP queue/RPC call
paths in `message_queue_cpu.c` and `kernel_gsp.c` from
[NVIDIA/open-gpu-kernel-modules](https://github.com/NVIDIA/open-gpu-kernel-modules).

This is transport framing only. It is not wired to a booted GSP, does not yet
implement the payload schema for NVIDIA's CONTINUATION_RECORD RPC function for
messages exceeding 16 pages, and does not implement RPC function schemas or
hardware doorbell register programming. The MSGQ 2.0 layer now has both a wire-layout helper and a register-driven
single-producer/single-consumer queue. It creates or validates the 32-byte
NVIDIA header, calculates fixed-size slots, reads peer counters through supplied
MMIO callbacks, enforces monotonic head/tail capacity rules, wraps slot addressing,
and publishes counter updates only after the configured memory-ordering callback.
Register offsets remain platform inputs and are not guessed. The ring caller must still
supply software counter adapters backed by verified queue-head/tail registers
and the notification callback.

### Bounded GSP runtime ELF parser

The runtime-firmware path now has a separate ELF64 little-endian RISC-V parser.
It validates the ELF header, program-header table, section-header table, string
table, every file-backed section range, load-segment sizes/alignment, and an
executable load segment covering the entry point. It locates exactly one
caller-selected signature section (for example, the Ampere `.fwsignature_ga10x`
section) and returns its bounded file range plus validated load-segment
metadata. Tests cover malformed headers/tables, out-of-range segments, bad
entry points, missing signatures, and broken string tables.

The parser only locates and bounds the signature data; it does not authenticate
the cryptographic signature, apply firmware-specific relocation, or execute the
image.

### Bounded GSP booter-image parser

A read-only parser validates the version-2 signed booter-load container before
a future loader uses its offsets. It checks the outer binary size, payload
range, security-header range, signature table/count, patch metadata pointers,
minimum metadata fields, load-header size, application count, and every code/data
segment range. It extracts fuse/engine/ucode metadata and the first application
descriptor without modifying firmware bytes. Synthetic tests cover truncated
input, invalid offsets, malformed signature counts, missing metadata, invalid
application ranges, and unsupported component types.

Structural validation does not authenticate NVIDIA's signature cryptography,
patch signatures, load Falcon IMEM/DMEM, boot the GSP, or establish an RPC queue.
Execution remains disabled until hardware-profile checks and trusted signature
handling are implemented.

## Design goals

- Keep generic GPU lifecycle and display contracts independent of NVIDIA details.
- Preserve the bootloader framebuffer as a fallback if NVX is absent or fails.
- Treat PCI identification as candidate discovery, not proof of driver support.
- Keep the kernel freestanding; do not assume hosted runtime or Linux kernel APIs.
- Validate hardware-derived sizes, addresses, modes, firmware, and command bounds.
- Keep future user-mode graphics APIs separate from the hardware-driver ABI.
- Add no linked stubs and advertise no unsupported capability.

## Source ownership

| Path | Responsibility |
| --- | --- |
| drivers/gpu/core/ | Generic GPU lifecycle, capability and resource contracts |
| drivers/gpu/display/ | Display modes and presentation, separate from command execution |
| drivers/gpu/nvx/ | NVIDIA discovery and future family initialization, firmware, memory, queues and interrupts |
| include/gpu.h | Generic identity, lifecycle and capability vocabulary |
| include/gpu_display.h | Display-mode and presentation contracts |
| include/nvx_device.h | NVIDIA identity, profile recognition, descriptor and discovery API |
| include/nvx_lifecycle.h | NVX-specific fail-closed lifecycle state machine |
| include/nvx_resource.h | Validated BAR metadata and MMIO acquisition contract |
| include/nvx_mmio.h | Bounded register access over an existing mapping |
| include/nvx_attach.h | BAR0 mapping, PMC probe and teardown transaction |
| include/nvx_firmware.h | Verified firmware-provider acquisition contract |\n| include/nvx_dma.h | Bounded, contiguous NVX DMA allocation and lifetime |\n| include/nvx_gpuva.h | GPU virtual-address reservation allocator |\n| include/nvx_gpuvm.h | 49-bit V2 GPU page-table construction and bounded MMU flush |\n| include/nvx_object_memory.h | Bounded DMA-backed object ownership and GPU VA reservation |\n| include/nvx_sync.h | Monotonic CPU-side timeline/fence contract |\n| include/nvx_submit.h | Opaque batch and relocation metadata validation |\n| include/nvx_irq_caps.h | Read-only MSI/MSI-X capability and table bounds |
| include/nvx_memory.h | Explicit CPU, system-physical and GPU address-space contracts |
| include/nvx_firmware.h | Injected firmware-provider interface independent of VFS paths |
| include/nvx_command.h | Queue, command-buffer and completion-state vocabulary |
| drivers/gpu/*/Makefile | Per-layer source ownership; root build consumes aggregate list |

## Dependency direction

    Applications / future graphics API
                    |
          graphics service / compositor
                    |
          generic GPU + display contracts
                    |
             +------+------+
             |             |
       boot framebuffer   NVX
                            |
                  PCI / MMIO / DMA / IRQ
                            |
                  x86-64 kernel facilities

Generic GPU code must not include NVX-specific headers. NVX may depend on generic
GPU contracts and existing PCI, MMIO, DMA, interrupt, memory and serial logging
interfaces. NVX must not take over the existing framebuffer renderer or make
boot depend on discrete-GPU initialization.

## Contracts and safety invariants

### Discovery and lifecycle

Use the existing PCI registry. Discovery reads identity and configuration-space
metadata only. BAR addresses are not mapped until resource type, size, alignment,
address width and overflow conditions can be validated. PCI enumeration and
command-register operations remain owned by the PCI subsystem. NVX owns
NVIDIA-specific interpretation after receiving a PCI device.

The lifecycle state machine permits only:
`DISCOVERED -> PROBING -> RESOURCES_READY -> INITIALIZING -> INITIALIZED`,
then optional `RUNNING -> QUIESCING -> STOPPED` (or direct quiescing from
initialized). Any nonterminal state can fail closed with a nonzero error; failure
is terminal and clears verified capability bits. Capability bits may be published
only after initialization. These gates are deliberately not wired to the current
discovery-only path.

### MMIO, interrupts and DMA

BARs are physical resources, never directly usable virtual pointers. Map MMIO
through the kernel mapping API with explicit bounds and teardown. DMA allocations
must distinguish CPU virtual and physical addresses. Do not enable bus mastering
until DMA-visible state is valid. Interrupt handlers should do bounded work and
defer longer processing.

### DMA buffers\n\n`nvx_dma_alloc()` obtains a physically contiguous run using the kernel DMA\nallocator, validates every page and both physical/virtual continuity, rounds the\nrequested byte length to pages, caps each object at 64 pages (256 KiB), and checks\nthe complete address range against the caller-declared 32-bit or 64-bit device\naddress width. The current kernel DMA allocator is identity-mapped and below 4 GiB;\nthe NVX layer does not assume future allocators retain that limitation. Failed\nvalidation releases the whole allocation, and `nvx_dma_release()` is idempotent\nfor an already-released/zero-initialized handle. Allocation does not enable PCI\nbus mastering or submit work to the GPU. This is memory plumbing only: no GSP\nfirmware launch, GPU virtual-memory page tables, command queues, or cache/DMA\nbarrier protocol is implemented by this layer.\n\n### Firmware\n
Firmware is injected through a provider rather than hard-coded filesystem paths.
Validate device-family compatibility, lengths and integrity before use. Missing
or rejected firmware must fail initialization cleanly and preserve fallback
display output.

### Memory and command submission

System physical, CPU virtual and GPU virtual addresses are different address
spaces. Memory objects must eventually define alignment, access, residency,
lifetime and synchronization. Queue submission must use driver-owned command
buffers, bounded lengths, validated references and explicit completion/error
states. Untrusted applications must not write arbitrary registers or submit
unchecked commands.

### Display and rendering

Scanout and 3D acceleration are separate capabilities. Preserve the current
boot framebuffer first; later add safe mode discovery, modesetting, cursor
support and presentation. A future compositor or rendering API must not depend
on NVIDIA command encoding.

### Diagnostics and validation

The NVX firmware acquisition layer currently selects `gsp_ga10x.bin` for the recognized Ampere profile. It requires a platform firmware provider with request, release, and authenticity-verification callbacks, rejects empty or oversized blobs (over 64 MiB), and releases failed acquisitions. It does not implement a signature verifier itself and does not load or execute GSP firmware yet; the platform callback must perform trusted verification before any firmware can be accepted.

The focused NVX workflow compiles tests with `-Wall -Wextra -Werror` and
exercises PCI matching, exact family/profile IDs, unknown-ID fallback,
subsystem-ID handling, auxiliary-function filtering, diagnostics, bounded MMIO
reads/writes, conventional PCI capability-list traversal/cycle rejection, host-simulated 32/64-bit PCI BAR sizing and register restoration,
active-device sizing refusal, attach/probe rollback and teardown, firmware
acquisition and verification failures, mapping bounds and alignment, lifecycle
ordering, capability gating, shutdown and fail-closed behavior. These tests
validate software logic, not physical hardware. Use QEMU for generic boot
regressions and real hardware for RTX 3060 support claims. QEMU success cannot
prove physical NVIDIA support.

## Implementation phases

1. Generic GPU/display contracts, build ownership and architecture checks. (Merged.)
2. Read-only NVIDIA PCI inventory, conservative matching and mocked tests. (Implemented.)
3. Exact PCI family/profile recognition and lifecycle state gates. (Implemented; probe attach uses the lifecycle, while engine initialization remains unwired.)
4. BAR validation and size-gated MMIO mapping. (Guarded hardware sizing is implemented only when decode and bus mastering are disabled; live-device handoff remains unresolved.)
5. Bounded MMIO register access, attach/probe lifecycle and conservative PMC probing. (Implemented and covered by mocked tests; not hardware-validated.)
6. Bounded GSP firmware acquisition with mandatory platform verification callback. (Implemented; provider signature verification and firmware loading remain external/not yet integrated.)
7. Validated family-specific GSP/engine initialization and firmware launch.
8. Device memory management, DMA, interrupts and synchronization.
9. Command queues, bounded submissions and completion handling.
10. Display modesetting and scanout integration.
11. 2D/3D acceleration, shader execution and user-mode graphics interfaces.

Each phase is a gate, not a promise. Only tested capabilities may be reported as
supported.
