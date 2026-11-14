# First controlled hardware boot: IdeaPad 530S

This procedure is for a **non-installing UEFI live test**. It is not an installation guide. The objective is to boot the kernel from a separate USB device, retain the firmware-provided display mode, collect hardware inventory, and verify that VantaOS does not write to detected disks.

## Build artifact

The maintained GitHub Actions workflow is `.github/workflows/hardware-boot-media.yml`.

1. Open **Actions → VantaOS safe UEFI hardware test media**.
2. Run the workflow manually, or use the artifact from a successful master push.
3. Download the artifact named `VantaOS-UEFI-hardware-test-<commit>`.
4. Verify the image against `SHA256SUMS.txt` before writing it to removable media.

The image is a GPT disk image containing one FAT32 EFI System Partition (the standard EFI System Partition type GUID) with:

- `EFI/BOOT/BOOTX64.EFI`: VantaOS UEFI loader.
- `EFI/VANTA/KERNEL.ELF`: UEFI kernel image.

It is deliberately separate from `VantaOS-Setup.img`. The setup image contains the destructive installer; **do not use that image for this first test**.

CI validates the raw GPT/FAT32 image, its EFI System Partition, and the exact loader/kernel files. OVMF's loader smoke test uses a separate El Torito EFI image containing those same two binaries. That exercises the VantaOS EFI loader and kernel handoff, but it does **not** prove that a particular firmware can discover the raw USB disk's partition. The first physical USB boot remains the test of that firmware-specific path.

## Storage safety contract

UEFI boot-info sets `VANTA_BOOT_FLAG_STORAGE_READ_ONLY`. The common block-device registration path applies `BLOCK_DEVICE_READ_ONLY` to every whole disk registered during a UEFI boot. Partition devices inherit the flag, and the common write API rejects writes before reaching a storage driver. Reads and inventory remain available.

This protects disks exposed through VantaOS's block-device layer. It is not a substitute for keeping backups, and it does not make the prototype suitable for installation. The first test image is boot-only and does not include the setup application. The raw USB image is not modified by the OVMF smoke test; OVMF boots the separate El Torito wrapper. Once VantaOS is running, every disk registered through its common block-device layer is read-only.

## Prepare the laptop

Before testing:

- Confirm the exact machine type from the bottom label or firmware information. “IdeaPad 530S” covers multiple configurations.
- Keep Windows and its internal SSD/NVMe untouched. Do not run the installer.
- Have a separate, empty USB drive for the test image. Writing the image erases that USB drive.
- Keep a Windows recovery method available and ensure important files are backed up.
- Disconnect other external storage devices so the test cannot confuse them with the boot USB.
- Use the firmware's one-time boot menu to select the USB's UEFI entry. Do not change boot order permanently or change storage-controller mode.
- **Secure Boot:** the current test loader is not signed by a key trusted by the laptop. If Secure Boot is enabled, firmware will normally reject it. Do not change Secure Boot settings unless the device owner approves, a Windows/device-encryption recovery key is available, and you understand how to restore the original setting. If those conditions are not met, do not attempt the physical boot. The OVMF loader smoke test can still validate the EFI loader/kernel path, but it does not establish that laptop firmware will accept an unsigned image. Restore the original Secure Boot setting after testing.

Use a trusted image-writing tool that supports raw disk images. Select only the intended USB device and verify its capacity/model in the tool before confirming. Never select the internal laptop drive.

## First boot acceptance criteria

A successful first boot is deliberately modest:

1. Firmware starts `EFI/BOOT/BOOTX64.EFI`.
2. The UEFI loader validates and loads the kernel.
3. GOP framebuffer handoff succeeds and the desktop appears.
4. The serial log (when a supported capture path is available) shows `VANTAOS_UEFI_STORAGE_POLICY_READ_ONLY` and `VANTAOS_BOOT_OK`; without serial capture, record the visible desktop/panic result and do not claim the marker was observed.
5. CPU, SMBIOS, ACPI and PCI inventory are emitted without a panic or reboot.
6. External USB keyboard/mouse work if the relevant xHCI/HID devices initialize.

The internal NVMe, touchpad, Wi-Fi, audio, UHD 620 acceleration and MX130/MX150 acceleration are **not** acceptance requirements for this first boot. The touchpad and native GPU drivers remain incomplete.

## Capture useful evidence

If the laptop reaches the desktop, record:

- Exact machine type and BIOS version (do not publish serial numbers or UUIDs).
- Commit SHA and SHA-256 of the test image.
- Whether the boot was UEFI and whether Secure Boot was enabled.
- Screen resolution and whether the desktop remained stable for several minutes.
- Whether external USB input worked.
- Serial output from an external serial/debug path if available, with MAC addresses, serials, UUIDs and personal identifiers redacted.
- Any panic code or last visible diagnostic marker.

Do not infer driver support from a successful boot. A boot with GOP proves firmware framebuffer handoff, not Intel GPU acceleration. Disk inventory proves discovery, not safe filesystem writes. One successful boot is only a first compatibility observation.

## Recovery

If the machine does not boot, remove the USB drive and use the normal Windows Boot Manager entry. The test procedure does not require installing to the internal disk, repartitioning it, or replacing its EFI boot files. If the machine hangs, power it off using the normal power button procedure and remove the test USB before retrying.

## Troubleshooting: desktop visible but no input

Open the diagnostics screen with **Ctrl + Shift + F9** (needs a working keyboard; if none works, use an external USB keyboard) and read the *Interrupts and PS/2 input* block:

| Row | Meaning |
| --- | --- |
| IRQ route | `ioapic` (ACPI MADT found, normal), `virtual-wire` (8259 through LINT0, used when ACPI/IOAPIC is unusable), `pic` |
| Timer IRQ0 | `ok`, `recovered (virtual wire)` (the IOAPIC route was silent and the kernel switched), or `DEAD - polling mode` (no timer interrupt at all; input still works by polling, scheduling does not) |
| PS/2 keyboard scanning | `acknowledged` means the keyboard accepted *enable scanning*; `no ACK` points at the 8042/keyboard itself |
| PS/2 bytes via polling | Nonzero while keys work but interrupts do not: IRQ1/IRQ12 delivery is broken on this machine |

Serial markers (if captured): `VANTAOS_IRQ_TIMER_OK`, `VANTAOS_IRQ_TIMER_RECOVERED_VIRTUAL_WIRE`, `VANTAOS_IRQ_TIMER_DEAD_POLLING_MODE`, `VANTAOS_APIC_VIRTUAL_WIRE_OK`, `VANTAOS_PS2_KEYBOARD_SCANNING_ENABLED`.

These recovery paths are verified only under QEMU. Report the screen contents for a real machine rather than assuming it works.
