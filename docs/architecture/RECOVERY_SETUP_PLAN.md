# Recovery and Setup Implementation Plan

## Current architecture findings (2026-10-21)

- The BIOS path is GRUB/Multiboot 1. `isodir/boot/grub/grub.cfg` currently sets `timeout=0`, selects entry 0, and starts `/boot/kernel.bin` without a kernel command line.
- The separate UEFI path is a custom GNU-EFI loader. `boot/uefi/loader.c` loads `\\EFI\\VANTA\\KERNEL.ELF` directly and calls the UEFI kernel entry; it does not use GRUB configuration.
- The common startup path is `kernel_main()` / `kernel_main_uefi()` -> `kernel_initialize()` -> `os_initialize()` -> desktop. The Multiboot structure exposes a command-line pointer, but the normalized UEFI boot contract currently has no recovery-selection field.
- The existing framebuffer API is declared in `include/graphics.h`; it provides clear, rectangle, text, presentation, resolution, and panic preparation functions. The panic path is intentionally fatal and must remain separate from interactive recovery.
- Keyboard support in `include/keyboard.h` already provides character input and normalized navigation events. Recovery should consume that existing queue.
- Kernel initialization brings up memory, paging, APIC/SMP, PCI, heap, and drivers before OS initialization. The OS then initializes keyboard, graphics, filesystem/VFS, process/scheduler, mouse/USB, shell, and desktop.
- Existing diagnostics include serial output and a serial-log ring. Storage includes ATA/block-device and partition scanning work, plus FAT32/VFS operations; these facts alone do not prove a safe general-purpose partitioner or a complete installer.
- CI is defined primarily in `.github/workflows/build.yml`, with a separate `.github/workflows/nvx-tests.yml`. The build workflow builds the kernel/ISO and exercises QEMU and host regression tests.

## Implementation sequence

1. Add an explicit GRUB recovery boot entry while keeping the normal entry first/default. Pass a documented boot option to the kernel; do not display recovery during an ordinary boot.
2. Parse recovery selection at the shared kernel/OS boundary and enter recovery only after the existing keyboard and framebuffer are initialized, but before process/scheduler/desktop startup. Keep panic handling on the existing halt path.
3. Implement a bounded, keyboard-only recovery UI using the current graphics and keyboard APIs. Expose only actions with real backing implementations. Diagnostics should use current boot information and existing serial-log interfaces.
4. Add a screenshot action backed by a verified capture path. QEMU monitor screenshots must be format-checked and converted explicitly when the monitor output is not PNG. Do not claim guest-side persistent screenshot saving unless a supported writable destination is implemented.
5. Add regression tests to GitHub Actions proving default boot does not enter recovery and explicit recovery renders a recognizable screen. Preserve normal BIOS and UEFI startup.
6. Treat UEFI recovery selection as a separate integration item because the current UEFI loader bypasses GRUB and the normalized boot contract does not yet encode a recovery request.
7. Implement installation only after a disposable QEMU disk, supported partition layout, safe writes, boot-mode-specific bootloader installation, and post-install reboot verification are covered by CI. Until then, do not expose a destructive or fake installer.

## Safety and scope boundaries

- Do not invoke the interactive recovery UI from the fatal panic path.
- Do not format a detected disk automatically or infer that FAT32 write support implies partitioning support.
- Keep recovery UI, boot selection, diagnostics, screenshot capture, and installer storage logic in separate modules.
- No local checkout or local build is part of this workflow; GitHub Actions results are the validation gate.
