# Getting started

Status: documented procedure, matches the repository quick start. Last reviewed: 2026-11-13.

This guide takes you from a clean checkout to a booting VantaOS in QEMU, then
points to the next step for your goal.

## 1. Requirements

A Linux host (CI uses Ubuntu). On Debian or Ubuntu:

```sh
sudo apt-get update
sudo apt-get install --yes build-essential nasm grub-pc-bin xorriso mtools \
    dosfstools qemu-system-x86 gnu-efi ovmf gdisk
```

Windows and macOS users should use a Linux VM or WSL2. QEMU boots work there,
but real-device passthrough does not (see
[NVX passthrough testing](gpu/nvx/PASSTHROUGH_TESTING.md)).

## 2. Build and boot

```sh
git clone https://github.com/ItsDropper/VantaOS.git
cd VantaOS
chmod +x build.sh run.sh
./build.sh      # kernel.bin, VantaOS.iso, vantaos.img (128 MiB FAT32 disk)
./run.sh        # boots the ISO in QEMU
```

`vantaos.img` is disposable development data.

Tune the guest with environment variables:

| Variable | Default | Meaning |
| --- | --- | --- |
| `VANTAOS_CPUS` | `4` | Virtual CPUs (1–32) |
| `VANTAOS_MEMORY` | `512M` | Guest RAM |
| `VANTAOS_ACCEL` | `auto` | `kvm`, `hvf`, `whpx`, `tcg` |
| `VANTAOS_CPU` | `host` / `max` | QEMU CPU model |

## 3. Run the checks

Host-side unit tests and CI scripts are listed in [Testing](build/TESTING.md).
Quick local checks:

```sh
python3 scripts/check_architecture.py     # subsystem ownership boundaries
python3 scripts/check_syscall_abi.py      # public syscall table vs kernel dispatch
python3 scripts/check_doc_links.py        # documentation links
python3 scripts/nvx_knowledge.py check    # NVX hardware-knowledge database
```

## 4. Where next

| Goal | Document |
| --- | --- |
| Learn the structure | [Architecture overview](architecture/ARCHITECTURE.md) |
| Write a user program | [User-mode programs](architecture/user-mode-programs.md), [wrapper table](user-mode.md) |
| Use the shell | [Shell](architecture/SHELL.md) |
| Boot on real hardware | [First hardware boot](project/FIRST_HARDWARE_BOOT.md) |
| Work on the GPU driver | [GPU documentation](gpu/README.md) |
| Submit a change | [Contributing](../CONTRIBUTING.md) |

## Troubleshooting

| Symptom | Check |
| --- | --- |
| `build.sh` fails on missing tool | Re-run the `apt-get install` line above |
| QEMU is slow | `VANTAOS_ACCEL=kvm` needs `/dev/kvm` access; otherwise TCG is used |
| UEFI boot fails | Ensure `ovmf` is installed; see [Build system](build/BUILD_SYSTEM.md) |
