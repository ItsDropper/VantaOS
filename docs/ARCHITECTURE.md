# Architecture overview

This is a source-oriented overview of the current prototype, not a promise that every subsystem is production-ready.

## Boot and initialization

1. `src/boot/boot.asm` provides the Multiboot-compatible entry path and transfers control to the 64-bit kernel.
2. `src/kernel/kernel.c` initializes serial diagnostics, the GDT, physical-memory bookkeeping, interrupt infrastructure, paging, PCI/ATA discovery, and the heap.
3. `src/os/os.c` initializes terminal/input, graphics, filesystem/VFS, process management, scheduler, shell, and desktop components.
4. The OS draws an initial desktop frame, enables interrupts, writes the `VANTAOS_BOOT_OK` serial marker, and enters the desktop process loop.

The exact order matters: components rely on earlier initialization. When adding a subsystem, document its dependencies and add a test for both success and expected failure behavior.

## Main source areas

| Path | Responsibility |
| --- | --- |
| `src/boot/` | Boot assembly and Multiboot handoff. |
| `src/kernel/arch/x86_64/` | Long-mode GDT, IDT, interrupt handlers, and PIC setup. |
| `src/kernel/core/` | Heap, physical-memory bookkeeping, paging, process table, scheduler, timer. |
| `src/kernel/drivers/` | Current PCI, ATA, keyboard, and mouse driver code. |
| `src/kernel/storage/` | Filesystem namespace, FAT32 read path, and VFS handles. |
| `src/kernel/graphics/` | Software graphics primitives, text, input integration, and UI drawing. |
| `src/os/terminal/` | Terminal, shell, and shell commands. |
| `src/os/desktop/` | Desktop process and file-explorer UI. |
| `include/` | Public interfaces shared by kernel and OS components. |
| `.github/workflows/` | Build and QEMU regression checks. |

## Processes and scheduling

The process table has fixed capacity and currently represents kernel execution contexts. Each process record contains a state, name, saved stack/context, entry function, and address-space pointer. The scheduler uses timer ticks and a simple ready-process selection strategy. The CI self-test checks selected state transitions and a process that returns and is later reaped.

**Important:** assigning an address-space pointer is not equivalent to implementing secure isolation. Current process execution is kernel-mode, and the page-table implementation does not yet provide a validated per-process user address-space security boundary. Do not describe this as a general-purpose, isolated process model.

## Memory and paging

Paging sets up identity mappings for low memory and high virtual windows used by the heap/framebuffer. Address-space structures exist, but user mappings, permission enforcement, and safe user-pointer validation remain incomplete. Memory protection must be proven with tests that attempt prohibited accesses; structure definitions alone are not evidence of isolation.

## Filesystem and VFS

The filesystem builds a standard in-memory namespace such as `/system`, `/home/user`, and `/etc`, and can import directory entries when FAT32 mounting succeeds. The VFS provides lookup/list/open/read/write/close interfaces and bounded open-file slots. However, the current filesystem create/write implementation is a stub that returns an unsupported result. Do not assume file creation or persistent writes work.

## Graphics and input

The graphics code draws the desktop using software primitives and consumes a Multiboot framebuffer when available. Keyboard and mouse driver code is present. CI currently verifies boot progress, not the visual correctness of each widget or full input behavior. Graphics success in QEMU does not imply accelerated GPU support.

## Diagnostics

- COM1 serial logging is used by CI and should remain bounded and safe during early boot.
- Panic/fault tracing is intended to help identify failures; it is not a replacement for debugging symbols or a debugger.
- CI markers use the `VANTAOS_..._OK` / `VANTAOS_..._FAIL` naming convention. A success marker should only be emitted after the corresponding assertion has actually passed.
