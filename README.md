# VantaOS

VantaOS is an experimental hobby operating system for x86-64, written in C and NASM. It is an engineering prototype, not a production operating system or a Windows replacement.

The project focuses on learning and validating operating-system fundamentals: boot and interrupt handling, memory management, kernel execution contexts, scheduling across CPUs, device discovery, a virtual filesystem, and a graphical shell. Its features have different maturity levels; see the documentation before assuming a capability is complete.

## Project status

- **Target:** x86-64, booted through GRUB/Multiboot for the current path.
- **Primary validation:** automated build checks, QEMU BIOS regressions, and UEFI/OVMF regressions in CI.
- **Current limitations:** kernel tasks are not isolated user-mode processes; general-purpose persistent file creation/writing is not implemented; networking, audio, modern GPU acceleration, and broad physical-hardware compatibility are not established.
- **Quality rule:** a source file or interface is not evidence that a feature is complete. Claims should identify what is actually tested.

## Start here

- [Documentation index](docs/README.md) — subsystem guides, build, testing, support, and plans.
- [Architecture overview](docs/ARCHITECTURE.md) — boot flow and subsystem boundaries.
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
