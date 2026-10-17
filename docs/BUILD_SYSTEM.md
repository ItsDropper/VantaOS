# Build system and source manifests

VantaOS uses a small GNU Make system inspired by subsystem-oriented kernel builds. It is Kbuild-inspired, not Linux-compatible Kbuild.

## Layout

- arch/: architecture-specific entry and interrupt code; x86-64 is the active target.
- kernel/: boot orchestration, diagnostics, scheduler, timer, and graphics code.
- kernel/proc/: process-table initialization, creation, context ownership, and lifecycle.
- kernel/selftest/: test-only boot-info/PMM and paging/spinlock probes.
- mm/: physical memory management, paging, and heap.
- drivers/input/input.c: bus-neutral input-device registry and bounded event queue.
- drivers/input/ps/2/: existing PS/2 keyboard and mouse implementations plus registry descriptors.
- drivers/input/usb/: USB input manifest and implementation plan; no USB input driver is claimed yet.
- drivers/pci/: PCI enumeration and device configuration.
- drivers/storage/: ATA/block storage code.
- fs/: filesystem namespace, FAT32, and VFS.
- os/: boot orchestration, terminal, shell, desktop, and file explorer.
- os/selftest/: test-only process, filesystem, memory, and SMP regression modules.
- include/: shared kernel interfaces.
- build/obj/: generated objects and dependency files, mirroring source paths.

## Input driver architecture

Transport-specific drivers register stable device IDs, bus/type metadata, and capability flags with the shared input core. They can publish normalized key, text, relative-motion, button, and wheel events through input_report_event(); consumers drain the bounded FIFO with input_next_event().

The registry and event queue use the kernel spinlock with IRQ save/restore and require no heap allocation. Registry capacity is fixed at 16 devices and the event queue holds 128 events. Queue overflow drops the newest event and increments input_dropped_event_count(). Unregistering a device removes its queued events. Device descriptors and their name strings must outlive registration.

The PS/2 drivers retain their established direct APIs so this change does not alter current desktop input behavior. Their metadata is registered after initialization, but they do not yet publish every keyboard/mouse action to the normalized event queue; that integration is a follow-up. The USB manifest is intentionally empty until xHCI enumeration and HID report parsing are implemented.

## Build commands

    make
    make list-sources
    make clean
    VANTAOS_SELF_TEST=1 make
    VANTAOS_SELF_TEST=1 ./build.sh

The root Makefile owns compiler flags, dependency generation, object placement, and final linking. Each subsystem Makefile declares its own source list. Compilation/linking alone does not prove the kernel boots; use CI QEMU and UEFI regression checks. The self-test flag changes the compiled kernel and should be used for test builds.

The arch/x86/ directory is legacy 32-bit source retained for audit; it is not linked by the current x86-64 target. Register each new source in the appropriate subsystem Makefile.
