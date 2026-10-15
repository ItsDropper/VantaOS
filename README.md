# VantaOS

VantaOS is an experimental hobby operating system for x86-64, written in C and NASM. It is an engineering prototype, not a production operating system or a Windows replacement yet.

## Repository architecture

- `arch/` — CPU architecture code and boot entry.
- `kernel/` — kernel initialization, diagnostics, process lifecycle, scheduler, timer, and software graphics.
- `mm/` — heap, physical memory bookkeeping, and paging.
- `drivers/` — keyboard, mouse, PCI, and ATA driver code.
- `fs/` — filesystem namespace, FAT32 read path, and VFS.
- `os/` — terminal, shell, desktop, and file-explorer UI.
- `include/` — shared interfaces.
- `docs/` — architecture, build, testing, hardware limits, and roadmap.

Presence in the source tree does not mean a feature is complete or validated. User-mode applications and per-process memory isolation are not implemented as a verified security boundary. General-purpose persistent file creation/writing is not implemented. Physical hardware compatibility, networking, audio, modern GPU acceleration, and broad USB support are not established.

## Build and run in QEMU

Tested in CI on Ubuntu Linux. Install the packages used by the workflow:

    sudo apt-get update
    sudo apt-get install --yes build-essential nasm grub-pc-bin xorriso mtools dosfstools qemu-system-x86

    git clone https://github.com/ItsDropper/VantaOS.git
    cd VantaOS
    chmod +x build.sh run.sh
    ./build.sh
    ./run.sh

The build creates `kernel.bin`, `VantaOS.iso`, and a 128 MiB `vantaos.img` if the image does not already exist. The image is a disposable development disk; do not put important data in it.

## Build commands

    make
    make list-sources
    make clean
    VANTAOS_SELF_TEST=1 ./build.sh

Generated objects and dependency files are stored under `build/obj/`. See [docs/BUILD_SYSTEM.md](docs/BUILD_SYSTEM.md).

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Build system](docs/BUILD_SYSTEM.md)
- [Testing and verification](docs/TESTING.md)
- [Hardware support](docs/HARDWARE_SUPPORT.md)
- [Development plan](docs/DEVELOPMENT_PLAN.md)
- [Security policy](SECURITY.md)

## Development principles

1. Work on a feature branch and keep a known-good backup.
2. Require a clean build and relevant runtime tests before merging.
3. Separate verified behavior from assumptions and planned features.
4. Never describe a kernel-mode process as an isolated user process.
