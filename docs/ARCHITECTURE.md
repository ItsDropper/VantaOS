# Architecture overview

This document describes the current prototype, not a promise that every subsystem is production-ready.

## Boot and initialization

1. GRUB loads the kernel through the Multiboot-compatible entry in `arch/x86_64/boot/boot.asm`, which creates early page tables, enters long mode, and transfers control to the kernel. VantaOS does not yet have its own bootloader; see [the bootloader plan](BOOTLOADER_PLAN.md).
2. `kernel/kernel.c` initializes serial diagnostics, GDT, physical-memory bookkeeping, interrupt infrastructure, paging, PCI/ATA discovery, and heap.
3. `os/os.c` initializes terminal/input, graphics, filesystem/VFS, process management, scheduler, shell, and desktop components.
4. The OS draws an initial desktop frame, enables interrupts, writes `VANTAOS_BOOT_OK` to serial, and enters the desktop process loop.

## Main source areas

| Path | Responsibility |
| --- | --- |
| `arch/x86_64/` | Active long-mode GDT, IDT, interrupt entry/dispatch, PIC, and boot code. |
| `arch/x86/` | Legacy 32-bit source retained for audit; not linked into the active target. |
| `kernel/` | Kernel initialization, serial diagnostics, panic/fault tracing, and graphics. |
| `kernel/proc/` | Process table and lifecycle. |
| `kernel/sched/` | Ready-process selection and scheduling. |
| `kernel/time/` | Timer subsystem. |
| `mm/` | Heap, physical-memory bookkeeping, and paging. |
| `drivers/` | Keyboard/mouse input, PCI enumeration, and ATA storage driver code. |
| `fs/` | Filesystem namespace, FAT32 read path, and VFS handles. |
| `os/terminal/` | Terminal, shell, and commands. |
| `os/desktop/` | Desktop and file-explorer UI. |
| `include/` | Shared interfaces. |
| `build/obj/` | Generated object/dependency tree; never edit generated files. |

## Processes and scheduling

The process table has fixed capacity and currently represents kernel execution contexts. Process records contain a state, name, saved stack/context, entry function, and address-space pointer. The scheduler uses timer ticks and a simple ready-process selection strategy. CI tests selected lifecycle transitions and a returning kernel process that is reaped.

An address-space pointer is not proof of secure isolation. Current process execution is kernel-mode; per-process user permissions and a validated user-address-space boundary are not implemented.

## Memory and paging

Paging establishes identity mappings for low memory and high virtual windows used by the heap/framebuffer. Address-space structures exist, but user mappings, permission enforcement, and safe user-pointer validation remain incomplete.

## Filesystem and VFS

The filesystem builds an in-memory namespace such as `/system`, `/home/user`, and `/etc`, and can import directory entries when FAT32 mounting succeeds. The VFS provides lookup/list/open/read/write/close interfaces and bounded open-file slots. Current filesystem create/write operations return an unsupported result.

## Graphics and input

Graphics code draws the desktop using software primitives and consumes a Multiboot framebuffer when available. Keyboard and mouse driver code exists. CI verifies boot progress, not visual correctness or full input behavior. This does not imply accelerated GPU support.

## Diagnostics

- COM1 serial logging is used by CI.
- Panic/fault tracing helps diagnose failures but does not replace a debugger.
- CI markers use `VANTAOS_..._OK` and `VANTAOS_..._FAIL`. A success marker must only be emitted after its assertion passes.
