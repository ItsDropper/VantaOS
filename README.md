# VantaOS

VantaOS is an experimental, hobby-built x86-64 operating system written in C and NASM. It is an active systems-programming project, not a production operating system or a Windows replacement yet.

The current build contains a freestanding kernel, long-mode boot/interrupt code, basic paging and heap code, process/scheduler scaffolding, keyboard and mouse input, PCI/ATA code, a filesystem namespace with FAT32-reading code, a software-rendered desktop, a terminal/shell, and a file explorer UI. **A component being present in the source does not mean its full functionality is complete or validated.** See the support matrix and testing guide before relying on any feature.

## Current status

- **Build and QEMU boot:** verified by GitHub Actions.
- **Process API and selected scheduler lifecycle paths:** checked by CI-only kernel self-tests.
- **Filesystem namespace and invalid VFS-handle guards:** checked by CI-only kernel self-tests.
- **User-mode applications and per-process memory isolation:** not implemented/verified as a security boundary.
- **General-purpose persistent file creation/writing:** not implemented; current filesystem write/create entry points return an unsupported result.
- **Physical-hardware compatibility, networking, audio, modern GPU acceleration, and broad USB support:** not established.

This is an engineering prototype. Do not install it to a real disk, use it for important data, or assume its security model is suitable for untrusted programs.

## Build and run in QEMU

Tested in CI on Ubuntu Linux. Install the packages used by the workflow:

```sh
sudo apt-get update
sudo apt-get install --yes build-essential nasm grub-pc-bin xorriso mtools dosfstools qemu-system-x86
```

Clone the repository and enter it:

```sh
git clone https://github.com/ItsDropper/VantaOS.git
cd VantaOS
chmod +x build.sh run.sh
./build.sh
./run.sh
```

The build creates `kernel.bin`, `VantaOS.iso`, and a 128 MiB `vantaos.img` when the image does not already exist. The image is a disposable development disk; back up anything important before experimenting with any disk image. `run.sh` starts QEMU with the ISO and disk image.

To reproduce CI's self-test build:

```sh
VANTAOS_SELF_TEST=1 ./build.sh
```

The CI self-tests emit serial markers and are only compiled when `VANTAOS_SELF_TEST` is set. Read [Testing and verification](docs/TESTING.md) for exact assertions and limitations.

## Documentation

- [Architecture overview](docs/ARCHITECTURE.md)
- [Testing and verification](docs/TESTING.md)
- [Hardware support and limitations](docs/HARDWARE_SUPPORT.md)
- [Development roadmap](docs/DEVELOPMENT_PLAN.md)
- [Security reporting](SECURITY.md)

## Development principles

1. Make changes on a feature branch and keep a known-good backup.
2. Require a clean build and relevant runtime tests before merging.
3. Separate verified behavior from assumptions and planned features.
4. Prefer reproducible QEMU tests, then add hardware testing only when safe and practical.
5. Never call a kernel-mode process an isolated user process.

## Contributing

Small, reviewable pull requests are easiest to validate. Include the problem, design choice, how you tested it, test logs where relevant, and anything that remains unverified. Do not include personal data or secrets in commits, logs, screenshots, or issue reports.
