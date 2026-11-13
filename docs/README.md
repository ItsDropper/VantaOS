# VantaOS documentation

VantaOS is an experimental x86-64 operating system. This index is the entry
point to every maintained document. Each document states what it claims and
what evidence backs the claim; see the [documentation style guide](STYLE.md)
for the status vocabulary used throughout.

> **Reading rule.** Source presence is not validated behavior. When a document
> conflicts with code or CI, the code and reproducible tests win; fix the
> document and record the discrepancy.

## Start here

| If you want to… | Read |
| --- | --- |
| Build and boot VantaOS in QEMU | [Getting started](GETTING_STARTED.md) |
| Understand how the system is put together | [Architecture overview](architecture/ARCHITECTURE.md) |
| Know what works and what does not | [Status table in the README](../README.md#status) and [Hardware support](project/HARDWARE_SUPPORT.md) |
| Run it on real hardware safely | [First hardware boot](project/FIRST_HARDWARE_BOOT.md) |
| Test the NVIDIA (NVX) driver on a real GPU | [NVX passthrough testing](gpu/nvx/PASSTHROUGH_TESTING.md) |
| Contribute a change | [Contributing](../CONTRIBUTING.md), [Testing](build/TESTING.md), [Style guide](STYLE.md) |
| Look up a term | [Glossary](GLOSSARY.md) |

## Documentation map

### Architecture and interfaces

| Document | Covers |
| --- | --- |
| [Architecture overview](architecture/ARCHITECTURE.md) | Boot sequence, subsystem responsibilities, memory, VFS, diagnostics |
| [Engineering model](architecture/ENGINEERING_MODEL.md) | Evidence levels, subsystem contracts, concurrency review, test layers |
| [CPU features](architecture/CPU_FEATURES.md) | CPUID feature detection and gating |
| [IPC](architecture/IPC.md) | Message passing and the non-consuming receive contract |
| [Kernel trace](architecture/KERNEL_TRACE.md) · [Watchdog](architecture/KERNEL_WATCHDOG.md) | Diagnostics infrastructure |
| [AHCI bring-up](architecture/AHCI_BRINGUP.md) | SATA controller discovery and PRDT contracts |
| [Compositor](architecture/COMPOSITOR.md) · [Desktop UI](architecture/DESKTOP_UI.md) · [User-space GUI](architecture/USERSPACE_GUI.md) | Graphics ownership, window model, process boundaries |
| [User-mode programs](architecture/user-mode-programs.md) · [User-mode wrapper table](user-mode.md) | ELF loader, `int 0x80` ABI, freestanding C headers |
| [Shell](architecture/SHELL.md) | Terminal commands and limits |
| [Recovery setup plan](architecture/RECOVERY_SETUP_PLAN.md) | Design of the optional recovery environment |

Kernel synchronization primitives: [wait queues](WAIT_QUEUES.md),
[mutexes](KERNEL_MUTEXES.md), [semaphores](KERNEL_SEMAPHORES.md),
[barriers](KERNEL_BARRIERS.md), [events](KERNEL_EVENTS.md).

### Drivers and networking

| Document | Covers |
| --- | --- |
| [Driver compatibility roadmap](DRIVER_COMPATIBILITY.md) | Target device classes and ordering |
| [Networking architecture](NETWORKING.md) | Ethernet/ARP/IPv4/IPv6/UDP/DHCP/DNS/TCP, HTTP client |
| [Network adapter support](network-adapter-support.md) · [Secure networking](secure-networking.md) | NIC coverage; TLS roadmap |
| [USB and Bluetooth bring-up](USB_BLUETOOTH_BRINGUP.md) | HCI interface recognition |
| RTL8188EU Wi-Fi: [bring-up](rtl8188eu.md), [status](RTL8188EU_END_TO_END_STATUS.md), [PHY profile](RTL8188EU_PHY_PROFILE.md), [hardware testing](RTL8188EU_HARDWARE_TESTING.md), [validation](RTL8188EU_HARDWARE_VALIDATION.md), [architecture review](RTL8188EU_ARCHITECTURE_REVIEW.md), [independent implementation research](RTL8188EU_INDEPENDENT_IMPLEMENTATION_RESEARCH.md) | Adapter-specific records |

### GPU

The GPU subsystem has a vendor-neutral framework and independent NVIDIA (NVX),
Intel and AMD backends. NVX is by far the most developed; **none of it is
hardware-verified**.

| Document | Covers |
| --- | --- |
| [GPU documentation index](gpu/README.md) | Entry point, reading order by goal |
| [GPU framework](gpu/FRAMEWORK.md) | Vendor-neutral lifecycle; per-vendor status |
| [NVX overview](gpu/nvx/OVERVIEW.md) | Architecture, boot chain, status matrix |
| [NVX independence policy and knowledge base](gpu/nvx/INDEPENDENCE.md) | How hardware facts are established and tracked |
| [NVX passthrough testing](gpu/nvx/PASSTHROUGH_TESTING.md) | Running `nvx` diagnostics on a real GPU through VFIO |
| [NVX architecture (design record)](NVX_ARCHITECTURE.md) | Long-form design |

### Build, test and release

| Document | Covers |
| --- | --- |
| [Build system](build/BUILD_SYSTEM.md) | Targets, generated artifacts, source manifests |
| [Testing](build/TESTING.md) | Every automated check, and what it does *not* prove |
| [Installer plan](build/INSTALLER_PLAN.md) · [Recovery setup](build/RECOVERY_SETUP.md) | Installer and recovery environment |

### Project planning

| Document | Covers |
| --- | --- |
| [Development plan](project/DEVELOPMENT_PLAN.md) | Staged priorities |
| [Hardware support](project/HARDWARE_SUPPORT.md) | Tested configurations and limits |
| [First hardware boot](project/FIRST_HARDWARE_BOOT.md) | Controlled first physical test |
| [Bootloader plan](project/BOOTLOADER_PLAN.md) | Independent boot path (plan only) |
| [Reliability audit](project/RELIABILITY_AUDIT.md) | Known reliability gaps |
| [Anti-cheat compatibility plan](project/ANTI_CHEAT_COMPATIBILITY_PLAN.md) · [Vanta Launcher compatibility](VANTA_LAUNCHER_COMPATIBILITY.md) | Long-term compatibility targets (plans, not support) |
| [Architecture decision records](decisions/README.md) | Rationale for long-lived choices |

### Formats and reference

[VZA archive format](VZA_ARCHIVE_FORMAT.md) · [Terminal command history](TERMINAL_COMMAND_HISTORY.md) ·
[IPC receive reliability (lesson)](IPC_RECEIVE_RELIABILITY.md) · [Glossary](GLOSSARY.md)

### Compatibility pointers

Files such as `docs/ARCHITECTURE.md` or `docs/TESTING.md` are three-line
pointers to the maintained copy so that old links keep working. Do not add
content to them.

## Maintaining the documentation

- Follow the [style guide](STYLE.md): every status claim names its evidence.
- Run `python3 scripts/check_doc_links.py` before committing. CI runs it too.
- Update the document in the same change as the behavior it describes.
