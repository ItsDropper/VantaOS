# Build system and source manifests

VantaOS uses a small GNU Make system inspired by the subsystem-oriented layout of larger kernels. It is Kbuild-inspired, not Linux-compatible Kbuild.

## Layout

- `arch/`: architecture-specific entry and interrupt code; x86-64 is the active target.
- `kernel/`: boot orchestration, diagnostics, process, scheduler, timer, and graphics code.
- `kernel/selftest/`: test-only boot-info/PMM and paging/spinlock regression probes.
- `mm/`: physical memory management, paging, and heap.
- `drivers/`: input, PCI, and ATA driver code.
- `fs/`: filesystem namespace, FAT32, and VFS; `fat32_lfn.c` owns long-filename sequence validation and decoding.
- `os/`: boot orchestration, terminal, shell, desktop, and file explorer.
- `os/selftest/`: test-only regression modules grouped by process, filesystem, memory, and SMP responsibility.
- `include/`: shared C interfaces.
- `build/obj/`: generated objects and dependency files, mirroring source paths.

The root Makefile owns compiler flags, dependency generation, object placement, and final linking. Each subsystem Makefile declares its own source list. `os/Makefile` explicitly registers the self-test translation units; `os/os.c` only calls their narrow entry points when `VANTAOS_SELF_TEST` is enabled. build.sh creates the bootable ISO and development disk image; CI runs the same build and QEMU smoke tests.

## Commands

    make
    make list-sources
    make clean
    VANTAOS_SELF_TEST=1 make
    VANTAOS_SELF_TEST=1 ./build.sh

Compilation/linking alone does not prove the kernel boots. Use the CI workflow and QEMU regression checks for boot evidence. The self-test flag changes the compiled kernel and should be used for test builds.

The `arch/x86/` directory is legacy 32-bit source retained for audit; it is not linked by the current x86-64 target. Do not add a source file without registering it in the appropriate subsystem Makefile.
