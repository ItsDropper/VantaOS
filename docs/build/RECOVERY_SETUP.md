# Recovery Environment (first milestone)

## BIOS/GRUB boot

The GRUB menu keeps **VantaOS** as the default entry. Choose **VantaOS Recovery Environment** before the five-second timeout to enter the keyboard-driven recovery UI. If no choice is made, GRUB starts the normal VantaOS entry.

The recovery entry passes the Multiboot command-line token `recovery=1`. The kernel checks that token after graphics, keyboard, filesystem, and VFS initialization and before process/scheduler/desktop startup. Choosing **Continue to VantaOS** returns to the ordinary startup sequence. Recovery is not entered by default, and the fatal panic handler never enters the recovery UI.

## Current actions

- **Continue to VantaOS** — returns to normal startup.
- **System diagnostics** — displays the active framebuffer dimensions/format, physical-memory block counters, PCI function count, and filesystem initialization state using live kernel APIs.
- **View boot and kernel logs** — displays a bounded snapshot of the serial-log ring. Full serial output is still available only through the configured serial endpoint.
- **Request QEMU screenshot** — emits `VANTAOS_RECOVERY_SCREENSHOT_REQUEST` on serial. The CI QEMU harness reacts by capturing the currently displayed framebuffer through the QEMU monitor, converts the monitor's PPM output to PNG, and validates the image with ImageMagick and OCR. This is a host-assisted QEMU capture, not a guest-side persistent screenshot file.
- **Restart computer** — attempts the legacy keyboard-controller reset and halts if the controller does not reset.
- **Shut down computer** — attempts the QEMU/Bochs ACPI shutdown ports and halts if unsupported.

Use **Up/Down** or **W/S** to move and **Enter** to select. The menu is designed to use the existing framebuffer and keyboard input path; it does not require a mouse.

## Scope and limitations

- The current recovery selection is implemented for the **BIOS/GRUB Multiboot path only**. The separate UEFI loader loads the kernel directly and does not yet carry a recovery request through the normalized boot contract. UEFI recovery selection must not be assumed to work.
- Screenshot capture currently depends on a host QEMU monitor. VantaOS does not yet write screenshots to persistent storage.
- Safe mode is intentionally not listed: optional-component boot policy is not implemented.
- The installer is intentionally not listed: a safe partitioning and bootloader installation pipeline has not been proven end-to-end against a disposable virtual disk.
- Recovery is for a running kernel that has reached the explicit recovery entry. It is not a way to resume after a fatal kernel panic or repair arbitrary kernel-memory corruption.
- Restart/shutdown are best-effort legacy/emulator operations; behavior on physical hardware varies and is not claimed as verified.

## CI verification

The build workflow boots the explicit GRUB recovery entry in QEMU, waits for the recovery serial marker, invokes the screenshot action, captures the current framebuffer with QEMU's monitor, explicitly converts PPM to PNG, verifies PNG dimensions and color diversity, and checks that OCR detects the recovery title and key menu items. CI artifacts retain the recovery screenshot for 14 days.
