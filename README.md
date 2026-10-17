# VantaOS

VantaOS is an experimental hobby operating system for x86-64, written in C and NASM. It is an engineering prototype, not a production operating system or a Windows replacement.

The project focuses on learning and validating operating-system fundamentals: boot and interrupt handling, memory management, kernel execution contexts, scheduling across CPUs, device discovery, a virtual filesystem, and a graphical shell. Its features have different maturity levels; see the documentation before assuming a capability is complete.

## Project status

- **Target:** x86-64. The established GRUB/Multiboot path and a separate UEFI/OVMF path are exercised by CI.
- **User-mode milestone:** VantaOS loads static ELF64 `ET_EXEC` programs from FAT32 into private address spaces and runs them at ring 3. The initial `int 0x80` ABI includes process spawn/wait, bounded file descriptors and I/O, directory listing, terminal input, file creation, and process-owned GUI windows.
- **Storage boundary:** bounded FAT32 8.3 regular-file create/write and multi-cluster range I/O are implemented and exercised by QEMU regression tests, including a second-boot persistence assertion. This is not general-purpose filesystem support: long-filename writes, directory creation, rename/delete, crash-atomic replacement, and broad corruption testing remain incomplete.
- **GUI boundary:** the kernel retains framebuffer/compositor ownership; user processes can request windows, submit bounded text/rectangle draw lists, and poll input/focus/close events. Terminal's shell runs as a ring-3 ELF, and Settings is now a separate ring-3 ELF at `/SYSTEM/SETTINGS.ELF`. Terminal's visible window/rendering and File Explorer remain kernel-side. Settings currently edits its own accent and compact-row preferences; it does not yet apply a system-wide desktop theme. The GUI ABI is a project-specific bring-up interface, not a stable platform API.
- **Still experimental:** process isolation needs broader fault and adversarial testing; demand paging, dynamic linking, networking, audio, modern GPU acceleration, and broad physical-hardware compatibility are not established.
- **Quality rule:** source presence is not proof of completion. See [Testing](docs/TESTING.md) for the exact automated assertions and their limits.

## Start here

- [Documentation index](docs/README.md) — subsystem guides, build, testing, support, and plans.
- [Architecture overview](docs/ARCHITECTURE.md) — boot flow and subsystem boundaries.
- [User-space desktop applications](docs/USERSPACE_GUI.md) — current GUI syscall boundary and migration plan.
- [User-mode programs](docs/user-mode-programs.md) — ELF loading, syscall ABI, and limitations.
- [Engineering model](docs/ENGINEERING_MODEL.md) — verification levels, contracts, concurrency, and test layers.
- [Architecture decision records](docs/decisions/README.md) — durable rationale for long-lived design choices.
- [Build system](docs/BUILD_SYSTEM.md) — build targets and generated artifacts.
- [Testing and verification](docs/TESTING.md) — CI checks and their limits.
- [Hardware support](docs/HARDWARE_SUPPORT.md) — known constraints and unverified areas.
- [Development plan](docs/DEVELOPMENT_PLAN.md) — staged project priorities.
- [Contributing](CONTRIBUTING.md) — engineering expectations and pull-request checklist.
- [Security policy](SECURITY.md) — reporting security concerns.

## Repository layout

| Path | Responsibility |
| --- | --- |
| `arch/` | CPU-specific entry, interrupt, and architecture code. |
| `boot/` | Boot-related assets and configuration. |
| `kernel/` | Kernel boot orchestration, diagnostics, process lifecycle, scheduling, timer, and graphics. |
| `kernel/selftest/` | Test-only boot-info, allocator, paging, and synchronization probes. |
| `mm/` | Heap, physical-memory bookkeeping, and paging. |
| `drivers/` | Keyboard, mouse, PCI, and ATA driver code. |
| `fs/` | Filesystem namespace, FAT32 read path, and VFS. |
| `os/` | Terminal, shell, desktop, and file-explorer UI. |
| `include/` | Shared interfaces. |
| `docs/` | Architecture, engineering standards, build, testing, support, and roadmap. |
| `.github/` | CI workflows and contribution templates. |

## Build and run in QEMU

The automated build is tested in CI on Ubuntu Linux. Install the packages used by the workflow:

```sh
sudo apt-get update
sudo apt-get install --yes build-essential nasm grub-pc-bin xorriso mtools dosfstools qemu-system-x86 gnu-efi ovmf gdisk

git clone https://github.com/ItsDropper/VantaOS.git
cd VantaOS
chmod +x build.sh run.sh
./build.sh
./run.sh
```

The build creates `kernel.bin`, `VantaOS.iso`, and a 128 MiB `vantaos.img` if the image does not already exist. Treat that image as disposable development data; do not store important files in it.

Useful build commands:

```sh
make
make list-sources
make clean
VANTAOS_SELF_TEST=1 ./build.sh
```

Generated objects and dependency files belong under `build/obj/`; see [Build system](docs/BUILD_SYSTEM.md).

## Development standards

1. Work on a focused topic branch.
2. Run the build and relevant regression tests before requesting review.
3. Preserve diagnostic markers used by CI and emit success markers only after assertions pass.
4. Document subsystem contracts when changing boot, memory, process, filesystem, or cross-CPU behavior.
5. Separate implemented, tested, verified, and planned capabilities in documentation.

See [Contributing](CONTRIBUTING.md) for the complete workflow and review checklist.
