# Build system and source manifests

VantaOS uses a small GNU Make system inspired by subsystem-oriented kernel builds. It is Kbuild-inspired, not Linux-compatible Kbuild.

## Layout

- arch/: architecture-specific entry and interrupt code; x86-64 is the active target.
- kernel/: boot orchestration, diagnostics, scheduler, timer, and graphics code.
- kernel/proc/: process-table initialization, creation, context ownership, and lifecycle.
- kernel/selftest/: test-only boot-info/PMM and paging/spinlock probes.
- mm/: physical memory management, paging, and heap.
- drivers/input/input.c: bus-neutral input-device registry and bounded event queue.
- drivers/input/ps/2/: existing PS/2 keyboard and mouse drivers, now publishing normalized events.
- drivers/input/usb/: HID boot-report decoder; xHCI transport and enumeration remain future work.
- drivers/pci/: PCI enumeration, BAR inspection, and command-register controls.
- drivers/storage/: ATA/block storage code.
- fs/: filesystem namespace, FAT32, and VFS.
- os/: boot orchestration, terminal, shell, desktop, and file explorer.
- os/selftest/: test-only process, filesystem, memory, and SMP regression modules.
- include/: shared kernel interfaces.
- build/obj/: generated objects and dependency files, mirroring source paths.

## Input driver architecture

Transport-specific drivers register stable device IDs, bus/type metadata, and capability flags with the shared input core. Drivers publish normalized key, text, relative-motion, button-transition, and wheel events through input_report_event(); consumers drain the bounded FIFO with input_next_event().

The registry and event queue use the kernel spinlock with IRQ save/restore and require no heap allocation. Registry capacity is fixed at 16 devices and the event queue holds 128 events. Queue overflow drops the newest event and increments input_dropped_event_count(). Unregistering a device removes its queued events. Device descriptors and their name strings must outlive registration.

The existing PS/2 keyboard and mouse remain the active desktop path. They now publish raw key transitions, translated text, relative motion, individual button transitions, and wheel deltas into the shared queue while preserving their legacy APIs. This is additive: current GUI input is not switched to consume the queue yet.

The USB HID module decodes boot-protocol keyboard and mouse reports and registers them as USB devices. Its caller must already have enumerated a USB device, selected HID boot protocol, and delivered complete reports. xHCI discovery/controller setup, DMA-safe memory, transfer rings, USB port/device enumeration, and interrupt-IN transfers are not yet implemented; therefore this is a real HID report driver layer, not yet end-to-end USB input support.

## Network initialization ownership

The kernel entry path initializes platform prerequisites (PCI, heap, and other
core facilities), then calls `net_stack_initialize()`. Network-specific
composition belongs to `net/init.c`, not `kernel/kernel.c`:

1. Reset and initialize the network core.
2. Register ARP, IPv4, IPv6, UDP, and TCP protocol handlers.
3. Register each PCI network driver's IDs and probe callback.
4. Probe PCI devices only after driver registration is complete.

This ordering keeps driver discovery deterministic and makes the network
subsystem's startup dependencies reviewable in one place. USB/xHCI startup
remains owned by the kernel platform initialization path because it is a
shared transport used by non-network devices as well.

## PCI resource APIs

The PCI layer exposes class-code search, BAR inspection (including 64-bit memory BARs and I/O BARs), and explicit memory-space and bus-master command-bit helpers. Enabling bus mastering is intentionally separate: a future DMA-capable driver must establish safe DMA buffers and controller state before calling it. BAR addresses are physical resources, not automatically mapped virtual pointers.

## Build commands

    make
    make list-sources
    make clean
    VANTAOS_SELF_TEST=1 make
    VANTAOS_SELF_TEST=1 ./build.sh

The root Makefile owns compiler flags, dependency generation, object placement, and final linking. Each subsystem Makefile declares its own source list. Compilation/linking alone does not prove the kernel boots; use CI QEMU and UEFI regression checks. The self-test flag changes the compiled kernel and should be used for test builds.

The arch/x86/ directory is legacy 32-bit source retained for audit; it is not linked by the current x86-64 target. Register each new source in the appropriate subsystem Makefile.
