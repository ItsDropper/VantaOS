# Hardware support and limitations

VantaOS is an experimental x86-64 OS. Source code for a driver is not proof that the driver works on all devices using that class of hardware.

## Current verification

| Area | Current evidence | Status |
| --- | --- | --- |
| x86-64 boot path | Kernel/ISO build and headless QEMU boot in CI | Verified in the tested QEMU configuration |
| Serial diagnostics | CI captures COM1 output and checks boot/test markers | Verified in QEMU |
| Timer/interrupt path | Scheduler probe executes and process reaping is observed in QEMU | Narrow path tested; not exhaustive |
| Graphics/framebuffer | Initialization code exists and boot logs report framebuffer details | Pixel correctness and broad firmware support unverified |
| Keyboard/mouse | Driver code exists | Device compatibility and full interaction unverified |
| PCI/ATA | Driver code exists and initialization runs during boot | Broad controller/device compatibility unverified |
| FAT32 | Directory-reading/mount code exists | Mount behavior depends on disk layout; write/create are not implemented |
| USB, Wi-Fi, Ethernet networking, audio | No end-to-end support established by the current CI | Not verified |
| Modern discrete GPU acceleration | No end-to-end driver support established | Not verified |

## Safe testing rules

- Use QEMU first and disposable disk images.
- Do not point experiments at a host system disk or any disk containing important data.
- Do not install this prototype to a physical disk or replace a working bootloader.
- Keep personal files and the host operating system separate from test images.
- When testing real hardware eventually, start with non-destructive live media and a recovery plan; do not claim support after one successful boot.

## Reporting a device test

Record the device model/revision, firmware settings, boot method, kernel commit, exact test steps, expected result, actual result, serial/panic logs, and whether the test was repeated. Redact serial numbers, UUIDs, MAC addresses, usernames, and other personal identifiers before publishing logs.

A useful support claim is specific, e.g. “boots to the desktop in QEMU with 256 MiB RAM under this workflow.” A claim such as “supports all PCs” is not justified by the current evidence.
