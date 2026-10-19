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

For display-controller candidates, BAR0 is reported as a raw address/type/width
only; its size is not inferred because the current PCI API does not expose BAR
sizing. The new `nvx_resource` layer validates BAR type, width, base alignment,
resource size, range overflow, and `size_t` conversion before mapping. Its MMIO
acquisition path requires an explicit, size-known memory resource and uses the
kernel's `mmio_map_region()` / `mmio_unmap_region()` contract. Since PCI currently
provides no trustworthy BAR size, discovery produces `SIZE_UNKNOWN` and mapping
is refused without calling the mapper. No live BAR sizing writes are performed.
The identity descriptor still leaves MMIO base and length unset. The new `nvx_attach` path consumes a caller-supplied BAR0 resource with trusted size metadata, validates it, maps it, checks PCI memory decoding, probes PMC_BOOT_0/PMC_BOOT_1, and retains the mapping only when the register aperture responds. Failure paths unmap and fail closed; detach unmaps and transitions through QUIESCING to STOPPED. A successful attach is explicitly `PROBED`, not `INITIALIZED`, and publishes no capabilities. This is real attach/probe orchestration, but the PCI subsystem still has no trusted BAR-size discovery path, so hardware discovery cannot yet supply the resource automatically.

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

### Firmware

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

The focused NVX workflow compiles tests with `-Wall -Wextra -Werror` and
exercises PCI matching, exact family/profile IDs, unknown-ID fallback,
subsystem-ID handling, auxiliary-function filtering, diagnostics, bounded MMIO
reads/writes, mapping bounds and alignment, lifecycle ordering, capability
gating, shutdown and fail-closed behavior. These tests
validate software logic, not physical hardware. Use QEMU for generic boot
regressions and real hardware for RTX 3060 support claims. QEMU success cannot
prove physical NVIDIA support.

## Implementation phases

1. Generic GPU/display contracts, build ownership and architecture checks. (Merged.)
2. Read-only NVIDIA PCI inventory, conservative matching and mocked tests. (Implemented.)
3. Exact PCI family/profile recognition and lifecycle state gates. (Implemented as software contracts; lifecycle not yet wired to hardware.)
4. BAR validation and size-gated MMIO mapping. (Validation and refusal paths implemented; trusted BAR sizing remains blocked by the PCI subsystem.)
5. Bounded MMIO register access and conservative PMC probing. (Implemented and covered by mocked tests; not hardware-validated.)
6. Firmware provider and validated family-specific hardware initialization.
7. Device memory management, DMA, interrupts and synchronization.
8. Command queues, bounded submissions and completion handling.
9. Display modesetting and scanout integration.
10. 2D/3D acceleration, shader execution and user-mode graphics interfaces.

Each phase is a gate, not a promise. Only tested capabilities may be reported as
supported.
