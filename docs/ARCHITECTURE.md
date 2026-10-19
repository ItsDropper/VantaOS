# Architecture overview

This document describes the current prototype, not a promise that every subsystem is production-ready. For verification levels and the expectations used when changing subsystem contracts, see the [engineering model](ENGINEERING_MODEL.md).

## System boundary map

The current design has two boot paths that converge on the kernel's normalized handoff contract. Static ELF64 applications run at ring 3, and a bounded VantaOS-specific GUI syscall ABI now supports process-owned windows, retained rectangle/text drawing, and queued events. The kernel still owns framebuffer access and composition. Settings, File Explorer, and Terminal now have ring-3 ELF applications using the bounded GUI syscall ABI; File Explorer supports bounded file operations through validated syscalls, while the kernel retains framebuffer and composition ownership. See [User-space desktop applications](USERSPACE_GUI.md).

```mermaid
flowchart TD
    GRUB[GRUB / Multiboot path] --> ENTRY[x86-64 kernel entry]
    UEFI[UEFI loader / OVMF path] --> HANDOFF[Normalized boot handoff]
    ENTRY --> HANDOFF
    HANDOFF --> INIT[Kernel initialization]
    INIT --> ARCH[CPU tables and interrupt infrastructure]
    INIT --> MM[Physical memory, paging, heap]
    INIT --> PROC[Process lifecycle and scheduler]
    INIT --> DEV[PCI and device drivers]
    MM --> FS[VFS and FAT32 read path]
    PROC --> OS[Kernel-integrated OS services]
    DEV --> OS
    FS --> OS
    OS --> UI[Terminal, shell, desktop, file explorer]
```

The diagram expresses intended subsystem relationships, not independent protection domains or a claim that every path is equally mature. GRUB remains the established fallback; UEFI/OVMF is exercised separately by CI. See the [bootloader plan](BOOTLOADER_PLAN.md).

## Boot and initialization

1. On the GRUB path, GRUB loads the kernel through the Multiboot-compatible entry in `arch/x86_64/boot/boot.asm`, which creates early page tables, enters long mode, and transfers control to the kernel. VantaOS does not yet have its own bootloader.
2. The UEFI path uses a separate EFI loader and kernel handoff. The OVMF regression checks loader execution, ELF loading, `ExitBootServices`, kernel entry, and the normalized handoff.
3. `kernel/kernel.c` owns boot-path adaptation: serial diagnostics, Multiboot normalization or UEFI handoff validation, and the transition to the OS startup path.
4. `kernel/init.c` owns ordered platform bring-up: GDT, PMM, interrupts, paging, APIC/SMP, PCI enumeration, and heap initialization. It hands off to `drivers_initialize()` only after PCI and heap prerequisites are ready.
5. `drivers/init.c` coordinates the network, USB, and storage subsystem entry points. Each subsystem owns its concrete registration and discovery sequence: `net/init.c`, `drivers/usb/init.c`, and `drivers/storage/init.c`.
6. `os/os.c` initializes terminal/input, graphics, filesystem/VFS, process management, scheduler, shell, and desktop components. The OS draws an initial desktop frame, enables interrupts, writes `VANTAOS_BOOT_OK` to serial, and enters the desktop process loop.

The two boot paths are not interchangeable evidence: a successful GRUB boot does not prove the UEFI path, and a successful OVMF boot does not prove physical firmware compatibility.

## Main source areas

| Path | Responsibility |
| --- | --- |
| `arch/x86_64/` | Active long-mode GDT, IDT, interrupt entry/dispatch, PIC, and boot code. |
| `arch/x86/` | Legacy 32-bit source retained for audit; not linked into the active target. |
| `boot/` | Boot-related assets and configuration. |
| `kernel/kernel.c` | Bootloader handoff adaptation, normalized boot-info ownership, serial diagnostics, and transition into OS startup. |\n| `kernel/init.c` | Ordered platform initialization and boot-stage timing; delegates concrete device startup to the driver coordinator. |\n| `drivers/init.c` | Cross-subsystem coordinator for networking, USB, and storage. It does not initialize individual controllers directly. |\n| `net/init.c`, `drivers/usb/init.c`, `drivers/storage/init.c` | Subsystem-owned startup order, registration, and discovery for their respective drivers/protocols. |
| `kernel/selftest/` | Test-only boot-info, platform, storage, and memory/process infrastructure regression probes. |\n| `net/selftest/` | Networking-owned regression probes, included only in self-test builds. |
| `kernel/proc/` | `process.c` owns table initialization/creation; `process_context.c` owns atomic CPU claims and context-switch transitions; `process_lifecycle.c` owns wake/block/terminate/reap and snapshots. `process_internal.h` is private shared state, not a public API. |
| `kernel/sched/` | Ready-process selection and scheduling. |
| `kernel/time/` | Timer subsystem. |
| `mm/` | Heap, physical-memory bookkeeping, address-space/page-table management, user mappings, and TLB shootdown coordination. |
| `mm/tlb_shootdown.c` | Serialized cross-CPU invalidation requests, acknowledgement handling, and fail-closed diagnostics. |
| `mm/user_mapping.c` | User page-table allocation/walking and validated map/unmap lifecycle. |
| `mm/paging_internal.h` | Private paging implementation contract shared by paging modules. |
| `drivers/` | Input, PCI enumeration, USB, storage, and hardware-driver registries. |
| `drivers/network/` | PCI network-driver matching and binding framework; concrete NIC drivers are not implemented yet. |
| `net/` | Bounded network-device registry, receive queue, and Ethernet-II demultiplexer; see [Networking architecture](NETWORKING.md). |
| `fs/` | Filesystem namespace, FAT32 read path, and VFS handles. |
| `fs/fat32_lfn.c` | FAT32 long-filename sequence validation and UTF-16-to-UTF-8 decoding. |
| `os/terminal/` | Terminal, shell, and commands. |
| `os/desktop/` | Desktop and file-explorer UI. |
| `os/selftest/` | Test-only process, filesystem, allocator, and SMP regression probes; separate from boot orchestration. |
| `include/` | Shared interfaces. |
| `build/obj/` | Generated object/dependency tree; never edit generated files. |

## Processes and scheduling

The process table has fixed capacity and supports both kernel execution contexts and an early user-process path. Process records track lifecycle state, saved context, kernel stack, address space, and—where applicable—user-image pages and entry/stack metadata. The scheduler uses timer ticks and ready-process selection; selected lifecycle, reaping, and SMP invariants are exercised in QEMU.

Static ELF64 `ET_EXEC` images can be loaded from the FAT32-backed shell path into a private user address space and entered at CPL3 (ring 3). The initial stack contains `argc`, a null-terminated `argv` array, an empty `envp`, and a minimal `AT_NULL` terminator. Argument count and string storage are bounded. The current `int 0x80` interface supports exit, output, bounded file descriptors and I/O, spawn/wait, directory listing, terminal input, file creation, and GUI window/draw/event operations. It is a bring-up ABI, not a stable POSIX ABI. See [User-mode programs](user-mode-programs.md) and [User-space desktop applications](USERSPACE_GUI.md).

This is an early protection boundary, not a claim of production-grade isolation. The ELF loader validates segment ranges and permissions and requires NX support, while output syscalls validate user buffers. Broader invalid-pointer, fault-containment, process-to-process, and adversarial testing remains necessary. SMP test success proves only the assertions those tests exercise, not general race freedom or long-duration stability.

## Memory and paging

Paging establishes identity mappings for low memory and high virtual windows used by the heap/framebuffer. The physical memory manager consumes the versioned, normalized boot-info memory map and reserves the kernel image, legacy handoff metadata, and framebuffer. The user ELF path allocates/maps bounded user pages with write/execute permissions and a writable, non-executable stack; syscall output buffers are checked against the current process address space. Demand paging, mature virtual-memory policy, and exhaustive mapping/fault validation remain unimplemented.

## Filesystem and VFS

The filesystem builds an in-memory namespace such as `/system`, `/home/user`, and `/etc`, and can import directory entries when FAT32 mounting succeeds. The VFS provides lookup/list/open/read/write/close interfaces and bounded open-file slots. User processes have a small per-process descriptor table and bounded syscalls for read/write/close, directory listing, and regular-file creation. FAT32 supports conservative 8.3 file writes and range-based multi-cluster I/O; CI checks write/readback and a second-boot persistence marker. This does not provide long-filename writes, directory creation, rename/delete, crash-atomic replacement, or general corruption recovery.

## Networking foundation

The initial networking core has a bounded device registry, synchronous TX contract, allocation-free bounded RX ingress queue, per-device counters, Ethernet-II EtherType dispatch, and PCI driver matching. It deliberately has no concrete NIC or IP protocol yet. See [Networking architecture](NETWORKING.md) for ownership and execution-context constraints.

## Graphics and input

Graphics code draws the desktop using software primitives and consumes a framebuffer when available. Keyboard and mouse driver code exists. CI verifies selected boot and kernel invariants, not pixel-level correctness or full input behavior. This does not imply accelerated GPU support.

## Cross-subsystem contracts

Changes that cross these boundaries should be reviewed together with their callers and tests:

- **Boot information:** loaders provide normalized, versioned data; memory management must validate ranges and preserve reserved-region precedence.
- **Interrupts and SMP:** interrupt state, per-CPU ownership, atomic ordering, CPU affinity, and lock ordering must remain explicit.
- **Process lifetime:** scheduler references must not outlive process records; wakeups and affinity changes must trigger the required scheduling work.
- **Filesystem handles:** invalid handles and unsupported operations must fail explicitly; namespace visibility is not equivalent to durable storage.
- **Diagnostics:** serial markers are test interfaces. Emit a success marker only after its associated assertion passes.

These are design constraints for review, not a claim that every edge case is already proven. See [Engineering model](ENGINEERING_MODEL.md) for the review questions and [ADR-001](decisions/0001-subsystem-source-manifests-and-boot-coordinators.md) for the source-boundary rationale.

## Test-code boundaries

`kernel/kernel.c` coordinates early kernel initialization; it does not own regression fixture implementations. Kernel-only probes live in `kernel/selftest/`, grouped by the contract they exercise:

- `boot_info_selftest.c` — Multiboot normalization and PMM boot-map invariants.
- `platform_selftest.c` — spinlock primitive, TLB shootdown, user mappings, and per-CPU address-space invariants.
- `selftest.h` — explicit entry points called by the boot coordinator only when `VANTAOS_SELF_TEST` is enabled.

`os/os.c` owns OS initialization order and the long-running desktop entry path. It should coordinate subsystems, not implement their regression fixtures. Test-only code lives under `os/selftest/`, divided by responsibility:

- `process_selftest.c` — process lifecycle, API guards, and process-record invariants.
- `filesystem_selftest.c` — VFS namespace and FAT32 integration checks.
- `memory_selftest.c` — physical and heap allocator checks.
- `smp_selftest.c` — cross-CPU scheduling, wakeup, and migration probes.
- `selftest.h` — the narrow interface used by boot orchestration in explicit self-test builds.

The self-test translation units are compiled in ordinary builds too, but their implementations are gated by `VANTAOS_SELF_TEST`. Add a probe to the subsystem-specific module rather than growing `os/os.c`. Keep success markers attached to assertions and registered in CI when they are required regressions.

## Diagnostics

- COM1 serial logging is used by CI.
- Panic/fault tracing helps diagnose failures but does not replace a debugger.
- CI markers use `VANTAOS_..._OK` and `VANTAOS_..._FAIL`; logs and the exact workflow configuration determine what a passing marker establishes.
