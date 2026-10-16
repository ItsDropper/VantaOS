# VantaOS

**VantaOS is an experimental x86-64 operating-system project written in C and NASM.** It is a learning and systems-engineering project, not a production OS or a Windows replacement.

[![Build and boot regressions](https://github.com/ItsDropper/VantaOS/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/ItsDropper/VantaOS/actions/workflows/build.yml)

> **Status:** Prototype. CI evidence applies to the specific emulated configurations listed in [Testing](docs/TESTING.md); it is not a claim of general hardware compatibility or security.

## What currently works

- GRUB/Multiboot x86-64 boot path and a separate UEFI/OVMF boot path.
- APIC-based interrupt infrastructure and SMP startup, per-CPU state, timer/reschedule IPIs, idle contexts, remote wakeups, and process migration.
- Kernel-mode process lifecycle and scheduler probes.
- Physical-memory bookkeeping, heap allocation, paging structures, and normalized boot information.
- Software-rendered desktop/terminal UI, input-driver code, FAT32 directory-read path, filesystem namespace, and VFS handles.

These are prototype capabilities, not a claim that every subsystem is complete. Automated checks and their exact limits are documented in [Testing and verification](docs/TESTING.md).

## Important limitations

- Processes execute in kernel mode; **there is no verified user-mode isolation boundary**.
- General-purpose persistent file and directory creation/writing is not implemented.
- Modern GPU acceleration, broad USB support, Wi-Fi, Ethernet networking, audio, and physical-device compatibility are not established.
- A successful emulator boot does not prove reliability on a physical machine.

Do not install VantaOS to a machine's internal disk or use it with irreplaceable data. Start with QEMU and disposable disk images.

## Build and run

The primary workflow is tested in GitHub Actions on Ubuntu Linux. Install the build and emulation dependencies:

```sh
sudo apt-get update
sudo apt-get install --yes build-essential nasm grub-pc-bin xorriso mtools dosfstools qemu-system-x86 gnu-efi ovmf gdisk
```

Clone and build:

```sh
git clone https://github.com/ItsDropper/VantaOS.git
cd VantaOS
chmod +x build.sh run.sh
./build.sh
./run.sh
```

The build creates `kernel.bin`, `VantaOS.iso`, and a 128 MiB `vantaos.img` if the image does not already exist. Treat the image as disposable development media. Keep important files outside it.

Useful targets:

```sh
make                 # Build the kernel
make list-sources    # Print registered source files
make clean           # Remove generated build products
VANTAOS_SELF_TEST=1 ./build.sh
make uefi            # Build the UEFI test image (requires UEFI build dependencies)
```

See [Build system](docs/BUILD_SYSTEM.md) for build outputs, source manifests, and dependency behavior.

## Repository map

| Path | Responsibility |
| --- | --- |
| `arch/x86_64/` | Active x86-64 entry, CPU setup, interrupts, and SMP architecture code |
| `arch/x86/` | Legacy 32-bit source retained for reference; not linked into the active target |
| `boot/uefi/` | UEFI application/loader |
| `kernel/` | Kernel core, process lifecycle, scheduler, timer, and diagnostics |
| `mm/` | Physical memory, heap, and paging |
| `drivers/` | Input, PCI, and ATA driver code |
| `fs/` | Filesystem namespace, FAT32 read path, and VFS |
| `os/` | Terminal, shell, desktop, and file explorer |
| `include/` | Shared interfaces and boot-information contract |
| `docs/` | Architecture, decisions, testing, build, and development guidance |
| `.github/` | CI workflow and contribution templates |

For the subsystem relationships and initialization sequence, start with [System design](docs/SYSTEM_DESIGN.md).

## Engineering documentation

- [System design and subsystem boundaries](docs/SYSTEM_DESIGN.md)
- [Architecture overview](docs/ARCHITECTURE.md)
- [Build system and source manifests](docs/BUILD_SYSTEM.md)
- [Testing and verification matrix](docs/TESTING.md)
- [Hardware support and limitations](docs/HARDWARE_SUPPORT.md)
- [Development roadmap](docs/DEVELOPMENT_PLAN.md)
- [Architecture decision records](docs/adr/README.md)
- [Contributing guide](CONTRIBUTING.md)
- [Security policy](SECURITY.md)

## Development standards

1. Keep changes small, scoped, and reviewable; work on a branch rather than directly on `master`.
2. Describe the behavior changed, evidence gathered, and known limitations in each pull request.
3. Treat compilation, emulator boot, targeted regression tests, and physical-hardware testing as different levels of evidence.
4. Never add a success marker unless the corresponding assertion has passed.
5. Document architecture changes and trade-offs when they affect subsystem boundaries or long-term interfaces.
6. Mark unrun tests as **not run**; do not infer success from a build alone.
