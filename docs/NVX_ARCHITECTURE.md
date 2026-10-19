# NVX GPU driver architecture

NVX is an independent, unofficial NVIDIA GPU driver project for VantaOS. It is
not produced, endorsed, or supported by NVIDIA. This document defines intended
source boundaries and contracts; it does not claim GPU detection, firmware
loading, modesetting, command submission, or acceleration is implemented.

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
| drivers/gpu/nvx/ | NVIDIA matching, family initialization, firmware, memory, queues and interrupts |
| include/gpu.h | Generic identity, lifecycle and capability vocabulary |
| include/gpu_display.h | Display-mode and presentation contracts |
| include/nvx_device.h | NVIDIA identity and device descriptors |
| include/nvx_memory.h | Explicit CPU, system-physical and GPU address-space contracts |
| include/nvx_firmware.h | Injected firmware-provider interface independent of VFS paths |
| include/nvx_command.h | Queue, command-buffer and completion-state vocabulary |
| drivers/gpu/*/Makefile | Per-layer source ownership; root build consumes aggregate list |

These headers establish proposed contracts only. No NVX implementation source is
linked by this architecture-only change.

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
GPU contracts and existing PCI, MMIO, DMA, interrupt, memory and logging
interfaces. NVX must not take over the existing framebuffer renderer or make
boot depend on discrete-GPU initialization.

## Contracts and safety invariants

### Discovery and lifecycle
Use the existing PCI registry. Validate device identity, revision, BAR shape and
length, and family requirements before touching registers. PCI enumeration and
command-register operations remain owned by the PCI subsystem. NVX owns NVIDIA-
specific interpretation after receiving a PCI device.

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
Each bring-up phase needs an observable state and failure reason through serial
logging. Test pure parsing/state transitions separately from hardware access.
Use mocked resources for unit tests, QEMU for generic boot regressions, and real
hardware for RTX 3060 support claims. QEMU success cannot prove physical NVIDIA
support.

## Implementation phases

1. Generic GPU/display contracts, build ownership and architecture checks.
2. PCI identification and safe, read-only device inventory.
3. Validated BAR mapping, lifecycle state machine and failure recovery.
4. Firmware provider and family-specific capability tables.
5. Device memory management, DMA, interrupts and synchronization.
6. Command queues, bounded submissions and completion handling.
7. Display modesetting and scanout integration.
8. 2D/3D acceleration, shader execution and user-mode graphics interfaces.

Each phase is a gate, not a promise. Only tested capabilities may be reported as
supported.
