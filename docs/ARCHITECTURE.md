# Architecture overview

This is a current-state overview of the prototype. For subsystem ownership, dependency direction, and SMP invariants, see [System design](SYSTEM_DESIGN.md). A source file's existence does not mean its feature is complete or hardware-validated.

## Boot and initialization

VantaOS currently has two boot paths:

1. **GRUB/Multiboot:** `arch/x86_64/boot/boot.asm` establishes early CPU state, enters long mode, and transfers control to the kernel.
2. **UEFI:** `boot/uefi/` provides an EFI application that loads the ELF64 kernel and transfers control after the UEFI handoff sequence.

Both paths are intended to supply a normalized, versioned boot-information structure. The GRUB path remains available as a fallback. The UEFI path is exercised under OVMF in CI; this does not prove support for all firmware.

At a high level, initialization establishes diagnostics and validates boot data, initializes CPU/interrupt and memory facilities, initializes supported drivers, starts process/scheduler and OS-facing subsystems, and reaches the desktop/process loop. The exact implementation order is defined by the current kernel and OS initialization code.

## Main source areas

| Path | Responsibility |
| --- | --- |
| `arch/x86_64/` | Active x86-64 boot entry, descriptor/interrupt mechanics, APIC/SMP primitives |
| `arch/x86/` | Legacy 32-bit source retained for reference; not linked into the active target |
| `boot/uefi/` | UEFI application/loader |
| `kernel/` | Kernel initialization, diagnostics, process lifecycle, scheduler, timer, graphics |
| `mm/` | Physical-memory bookkeeping, heap, paging |
| `drivers/` | Keyboard/mouse, PCI, and ATA driver code |
| `fs/` | Filesystem namespace, FAT32 read path, VFS handles |
| `os/` | Terminal, shell, desktop, file explorer |
| `include/` | Shared interfaces and boot-information contract |
| `build/obj/` | Generated objects/dependency files; never edit generated files |

Subsystem Makefiles declare source membership. The root Makefile owns common compiler/linker flags, dependency generation, object paths, and linking. See [Build system](BUILD_SYSTEM.md).

## Process and scheduler model

The fixed-capacity process table currently represents kernel-mode execution contexts. Process records include lifecycle state, a name, saved context/stack, entry function, and address-space-related state. The scheduler handles ready-process selection and state transitions; architecture code supplies CPU-specific mechanisms.

The active SMP work includes processor startup, per-CPU state and idle contexts, local timers, reschedule IPIs, remote wakeups, affinity/migration behavior, and scheduler stress checks. CI verifies selected invariants in 2- and 4-vCPU QEMU configurations. This is not exhaustive proof of race freedom or long-duration stability.

An address-space pointer is **not** proof of secure isolation. Controlled privilege transitions, correctly enforced user permissions, safe user pointers, and a verified user/kernel boundary are not implemented as a complete security model.

## Memory and paging

The physical memory manager consumes normalized boot memory information and reserves important ranges such as the kernel image and framebuffer. Heap allocation and paging structures are present, and CI checks selected allocation, accounting, mapping, and TLB-shootdown invariants.

These checks do not establish exhaustive malformed-input handling, complete user/supervisor permission enforcement, robust allocation-failure recovery, or process-to-process isolation.

## Filesystem and VFS

The filesystem provides an in-memory namespace, path lookup/listing, bounded VFS handles, and a FAT32 read-oriented path when mounting succeeds. General persistent file/directory creation and writing are not implemented; unsupported operations must remain explicit to callers.

## Graphics and input

The desktop is drawn using software primitives and a framebuffer when one is provided by the boot path. Keyboard/mouse, PCI, and ATA code exists, but CI does not establish full interaction correctness or broad physical-device compatibility. Modern discrete GPU acceleration, broad USB, networking, and audio support are not established.

## Diagnostics and evidence

- COM1 serial output is the primary machine-readable CI diagnostic channel.
- Panic/fault tracing assists debugging but does not replace a debugger.
- CI uses `VANTAOS_..._OK` and `VANTAOS_..._FAIL` markers.
- A success marker must only be emitted after the corresponding assertions pass.

See [Testing and verification](TESTING.md) for the tested QEMU matrix, UEFI/OVMF checks, and explicit coverage gaps.
