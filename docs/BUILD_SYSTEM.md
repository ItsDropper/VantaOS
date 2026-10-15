# Build system and source manifests

VantaOS uses a small GNU Make build system inspired by the separation of build policy and subsystem source lists found in larger kernels. It is Kbuild-inspired, not a copy of Linux Kbuild and does not provide Linux's configuration or module system.

## Responsibilities

- The root Makefile owns compiler flags, dependency generation, object placement, and the final kernel link.
- Subsystem Makefiles declare source files close to the code they build.
- Object and dependency files are written beneath build/obj/, preserving source paths and preventing same-named translation units from colliding.
- build.sh wraps the kernel build, ISO generation, and disposable FAT32 development-image setup.
- .github/workflows/build.yml builds with self-tests enabled and checks the resulting image in QEMU.

## Common commands

    make                 # build kernel.bin
    make clean           # remove generated objects and kernel.bin
    make list-sources    # inspect the effective source manifest
    VANTAOS_SELF_TEST=1 make
    VANTAOS_SELF_TEST=1 ./build.sh  # build kernel, ISO, and disk image for CI-style testing

A successful make proves compilation and linking only. Use build.sh and the documented QEMU checks to validate the boot path. The self-test flag changes the compiled kernel and should be used for test builds, not treated as a production configuration.

## Source manifest hierarchy

    Makefile
    src/boot/Makefile
    src/kernel/Makefile
    src/kernel/arch/Makefile
    src/kernel/arch/x86_64/Makefile
    src/kernel/core/Makefile
    src/kernel/drivers/Makefile
    src/kernel/drivers/input/Makefile
    src/kernel/drivers/pci/Makefile
    src/kernel/drivers/storage/Makefile
    src/kernel/graphics/Makefile
    src/kernel/storage/Makefile
    src/os/Makefile
    src/os/terminal/Makefile
    src/os/desktop/Makefile

The active build target is x86-64. The older src/kernel/arch/x86/ files are intentionally not linked by this target; they remain pending a separate audit of whether they should become a supported 32-bit target or be removed. Avoid adding new source files without adding them to the appropriate subsystem manifest.
