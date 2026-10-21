# VantaOS Setup — implementation plan

VantaOS Setup is a separate boot mode, not part of normal desktop startup or recovery. The first implementation target is UEFI/GPT in QEMU. BIOS/GRUB installation is a separate compatibility milestone; do not claim it works until its boot files can be deployed and rebooted from a disposable disk.

## Setup experience

1. **Welcome** — explain that installation modifies the selected target disk; offer Install and Exit.
2. **Language** — choose the setup UI language. The initial build may offer English and Finnish only if both are actually translated; never show untranslated language choices.
3. **Keyboard** — choose a supported keyboard map and test typing. Do not claim a layout changes OS input until the mapping is wired into the input path.
4. **Time zone and clock** — choose a geographic time zone (initial examples: Europe/Helsinki, UTC). Persist the IANA zone ID, not just a numeric offset, because daylight-saving transitions change offsets. Show the current UTC/local time and the chosen zone. If the kernel has no RTC/time-zone service, the setup must say the choice is saved for future time services and must not claim the running kernel clock was changed.
5. **Hardware checks** — report detected memory, CPU architecture, boot mode, storage devices, and required files. Distinguish a passed check from an unavailable/unsupported capability.
6. **Target disk** — show stable device name, capacity, transport, partition table, and whether it is the boot medium. Require an explicit disk selection and a second confirmation naming that disk. Never select a target implicitly.
7. **Partition plan** — preview the exact GPT layout and sizes before any write. The initial UEFI plan uses a FAT32 EFI System Partition and a separate FAT32 VantaOS system partition. Align partitions to 1 MiB, validate all arithmetic and device bounds, and reject disks that are too small or have unsupported sector sizes.
8. **Install** — after explicit confirmation, create GPT, format the partitions, deploy the EFI loader to EFI/BOOT/BOOTX64.EFI, deploy the kernel to EFI/VANTA/KERNEL.ELF, and write system files/configuration. Every stage must check errors and flush writes before reporting success.
9. **Review and reboot** — show the installation log, selected disk, layout, language, keyboard, and time zone. Reboot only after verifying that required files exist and the boot files can be read back.

## Safety invariants

- Never write to a real disk during development or CI. Automated destructive tests must use a newly created disposable QEMU raw image and must assert its exact path and size before attaching it.
- The installer must never use the boot/install media as the target; identify boot media by device metadata and fail closed if that identity is unknown.
- No automatic disk selection, implicit formatting, silent fallback to another disk, or success screen after a partial failure.
- Validate sector size, capacity, overflow, GPT CRCs, partition bounds, non-overlap, alignment, and device write/flush status.
- Keep a read-only Review stage before the irreversible Install confirmation. Show the exact target device and partition sizes in that confirmation.
- Initial milestone is UEFI/GPT only. Unsupported legacy BIOS targets must be clearly rejected rather than receiving incomplete boot files.
- Recovery and Setup remain separate boot modes. Neither is entered during ordinary startup.
- Use the existing UEFI loader and its EFI/VANTA/KERNEL.ELF contract. Do not invent a replacement bootloader. Verify the actual loader and kernel entry before deployment changes.
- The kernel currently exposes block-device write/flush APIs, partition discovery, and bounded FAT32 file writes, but does not yet have a verified GPT writer or FAT32 formatter. Until those exist and pass read-back/reboot tests, setup must not expose a destructive Install action.

## Implementation gates

1. **Pure layout planner:** calculate a GPT layout without I/O; strict host tests cover minimum disk size, alignment, overflow, bounds, and invalid sector sizes.
2. **GPT writer:** write primary/backup headers and entry arrays with CRC32; read them back and re-scan before formatting. Host image tests must corrupt headers/CRCs and verify fail-closed behavior.
3. **FAT32 formatter:** implement and test BPB, FSInfo, FAT copies, reserved sectors, root cluster, and volume bounds against disposable images. Verify with independent tools in CI.
4. **Setup boot mode and wizard:** add an explicit setup boot selection and a keyboard-accessible UI with live hardware checks, locale/keyboard/time-zone choices, and a review screen.
5. **Deployment:** install and read back EFI files, kernel, system payload, and configuration; boot the installed disk in OVMF with the installer ISO detached.
6. **Release gate:** retain CI logs, disk image inspection, boot serial logs, and screenshots. Do not merge until all gates relevant to the PR pass.

## Time-zone configuration contract

Persist a stable zone identifier such as Europe/Helsinki, not a hard-coded UTC offset. The configuration file must be bounded, versioned, and validated against the time-zone IDs shipped by VantaOS. The initial installer must not download zone data or depend on network access. If no zone database or RTC service exists yet, save the user's selection for the later time service and disclose that the active kernel clock remains UTC/unconfigured.
