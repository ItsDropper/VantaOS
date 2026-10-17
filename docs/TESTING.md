# Testing and verification

This document distinguishes what automation actually checks from what is still untested. A green CI run means the listed assertions passed; it does not prove every code path or every hardware configuration works.

## Continuous integration

Workflow: [`.github/workflows/build.yml`](../.github/workflows/build.yml)

On pushes to `master`, pull requests targeting the repository, and manual workflow runs, GitHub Actions:

1. Checks out the repository on a clean Ubuntu runner.
2. Installs GCC/build tools, NASM, GRUB utilities, FAT tools, and QEMU.
3. Builds the freestanding kernel, bootable ISO, and FAT32 development image with `VANTAOS_SELF_TEST=1`.
4. Asserts that `kernel.bin`, `VantaOS.iso`, and `vantaos.img` exist and are non-empty.
5. Boots the ISO in headless QEMU with serial output captured to a log.
6. Requires all success markers, rejects any `VANTAOS_*_FAIL` marker, and requires QEMU to remain running until the smoke-test timeout.
7. Runs the GRUB/Multiboot ISO regression in three configurations: 256 MiB RAM with 1 vCPU, 512 MiB with 2 vCPUs, and 512 MiB with 4 vCPUs. Multi-vCPU runs additionally require scheduler-stress, remote-wakeup, and process-migration markers. CI waits briefly after QEMU exits so the serial log is flushed before assertions run.
8. Builds the separate UEFI system image and boots it with OVMF, requiring loader, `ExitBootServices`, kernel-handoff, and selected kernel/SMP markers.
9. Compiles the static ring-3 Terminal, Settings, and File Explorer applications with `-Werror`, installs them in the FAT32 image, and captures a graphical boot screenshot artifact. A push to `master` refreshes tracked `media/desktop.png` from that screenshot.

## Runtime assertions

| Marker | What it establishes |
| --- | --- |
| `VANTAOS_BOOT_OK` | Initialization reached the OS run path and emitted its boot marker. |
| `VANTAOS_BOOT_INFO_READY` | The live GRUB/Multiboot handoff was normalized into the versioned boot-info structure. |
| `VANTAOS_BOOT_INFO_ADAPTER_OK` | The contract magic/version/size are correct; synthetic memory ranges and framebuffer fields normalize correctly; malformed entry sizes, overflowing region ranges, and null arguments are rejected. |
| `VANTAOS_PMM_BOOT_INFO_OK` | The physical allocator consumes the normalized contract, rejects invalid boot-info pointers, preserves reserved-region precedence over overlapping usable memory, and reserves kernel and legacy metadata pages. |
| `VANTAOS_PMM_ALLOCATOR_OK` | The physical allocator is initialized; two distinct aligned frames are allocated; invalid and duplicate frees are rejected; and free/used accounting returns to baseline. |
| `VANTAOS_HEAP_ALLOCATOR_OK` | The heap is initialized; aligned allocations preserve test data; invalid and duplicate frees are rejected; and usage accounting returns to baseline. |
| `VANTAOS_PROCESS_API_GUARDS_OK` | Selected invalid process API arguments are rejected. |
| `VANTAOS_PROCESS_NAME_OK` | An overlong process name remains null-terminated at the maximum name index. |
| `VANTAOS_PROCESS_STATE_TRANSITIONS_OK` | A test process transitions through ready, running, sleeping, wake-up, and termination/reaping paths. |
| `VANTAOS_FILESYSTEM_MUTATION_OK` | The disposable FAT32 image supports creating a directory and long-named file, renaming it with a changed extension, resolving the new name, and deleting both file and empty directory. |
| `VANTAOS_FILESYSTEM_NAMESPACE_OK` | The filesystem/VFS initializes, root and standard directories can be resolved, a missing path stays missing, and root listing returns without error. |
| `VANTAOS_FAT32_NAME_GUARDS_OK` | FAT32 filename mutation rejects the invalid names covered by the focused name-guard regression. |
| `VANTAOS_USER_FILE_IO_OK` / `VANTAOS_USER_FILE_WRITE_OK` | The ring-3 smoke path validates file-descriptor read/write and bounded regular-file creation/write behavior for the test fixture. |
| `VANTAOS_USER_FILE_RANGE_IO_OK` / `VANTAOS_FAT32_MULTICLUSTER_IO_OK` | User-mode range I/O and multi-cluster FAT32 reads/writes pass the targeted smoke assertions. |
| `VANTAOS_FAT32_WRITE_READBACK_OK` / `VANTAOS_FAT32_REBOOT_PERSIST_OK` | The disposable FAT32 image passes write/readback and a second-boot persistence assertion. |
| `VANTAOS_USER_GUI_SYSCALL_OK` | The ring-3 GUI smoke test passes its window/draw/event ABI checks, including malformed commands and invalid pointers. |
| `VANTAOS_SETTINGS_STORAGE_OK` | The ring-3 Settings self-test writes its expected configuration, closes it, reopens it, and verifies the readback. |
| `VANTAOS_SETTINGS_PERSIST_OK` | The Settings self-test found the expected configuration already present before rewriting it on the later boot, demonstrating the tested cross-boot persistence path. |
| `VANTAOS_DESKTOP_THEME_CONFIG_OK` | The kernel loaded the versioned desktop theme configuration from the persistent filesystem. |
| `VANTAOS_GUI_INPUT_REGRESSION_OK` | GUI selftests cover focus transitions, pointer click coordinates, queue overflow, invalid event output buffers, close requests, owner hide/restore, and theme-change notifications. |
| `VANTAOS_GUI_DEAD_OWNER_CLEANUP_OK` | A window owned by a terminated probe process is removed and its stale ID is rejected. |
| `VANTAOS_VFS_HANDLE_GUARDS_OK` | Invalid VFS read/write/close handles are rejected. |
| `VANTAOS_ARGV_STACK_OK` | The ring-3 smoke executable sees the expected argument count and validates `argv[1] = alpha` and `argv[2] = two words` from its initial stack. |
| `VANTAOS_ELF_VALIDATION_OK` | A zero-filled 64-byte buffer is rejected as an ELF image. This is a narrow invalid-magic smoke test, not comprehensive malformed-ELF coverage. |
| `VANTAOS_USER_PROCESS_FAULT` | The expected ring-3 fault path was reached and handled as a user-process fault rather than a kernel panic; this marker alone does not prove arbitrary-fault containment. |
| `VANTAOS_FILESYSTEM_EXEC_OK` | The FAT32-loaded user ELF reaches ring 3, performs the tested output calls, and reaches its expected completion marker. |
| `VANTAOS_SCHEDULER_PROBE_OK` | The scheduler runs the dedicated kernel-mode probe entry. |
| `VANTAOS_SCHEDULER_REAP_OK` | The observer sees the completed probe process removed from the process table. |

Kernel self-tests run inside the kernel. Separately, the workflow builds `tests/user/hello.S`, installs the ELF in the FAT32 image, and launches it through the shell's file-backed `exec` path. That user program checks its initial arguments, exercises serial/terminal output, and exits through the syscall path. These are deterministic smoke tests, not adversarial isolation tests or a general application compatibility suite.

## Run locally

On a compatible Linux development environment with the packages listed in the README:

```sh
VANTAOS_SELF_TEST=1 ./build.sh
```

Then boot headless and capture serial output:

```sh
timeout 12s qemu-system-x86_64 \
  -machine pc -m 256M \
  -drive file=VantaOS.iso,media=cdrom,format=raw \
  -drive file=vantaos.img,if=ide,format=raw \
  -boot order=d -display none \
  -serial file:boot.log -monitor none -no-reboot
```

Exit code 124 from `timeout` is expected for this smoke test: the guest is expected to remain alive through the test window. After QEMU exits, allow its serial file to flush before checking the markers. An earlier exit, missing marker, or failure marker is a test failure. Inspect `boot.log` when debugging.

## UEFI boot test

The workflow also builds `VantaOS-UEFI.img` and boots it with OVMF. The image contains the removable-media entry point at `EFI/BOOT/BOOTX64.EFI` and the separate `kernel-uefi.elf` at `EFI/VANTA/KERNEL.ELF`.

The UEFI smoke test requires serial markers proving that the loader started, validated and loaded the ELF64 kernel, successfully called `ExitBootServices`, transferred control to the UEFI kernel entry, validated the normalized handoff, and reached `VANTAOS_BOOT_OK`. The original GRUB ISO tests still run independently in the 1-, 2-, and 4-vCPU matrix described above.

This test does not yet establish physical-hardware compatibility or complete removal of legacy Multiboot-shaped metadata from OS services. GRUB remains the fallback until the UEFI path is repeatedly green and malformed-ELF regression tests are added.

## What is not yet covered

- Exhaustive process-state combinations, sustained preemption stress, race conditions, or long-duration stability.
- Exhaustive page-table permission tests, invalid syscall-pointer fuzzing, fault containment for arbitrary user programs, and adversarial process-to-process isolation tests.
- Comprehensive malformed ELF program-header cases, overlapping segments, integer-boundary cases, and loader allocation-failure cleanup.
- Physical-memory-map malformed-entry/overflow fuzzing, allocator fragmentation, allocation-failure recovery, and long-duration memory-pressure tests.
- Exhaustive filesystem corruption and power-loss recovery; the CI persistence marker covers only the tested disposable-image round trip.
- Pixel-level desktop correctness, end-to-end scripted mouse/keyboard interaction with the ring-3 apps, display modes, and full visual regression tests.
- Real-device compatibility, USB/HID edge cases, NICs, Wi-Fi, audio, GPU acceleration, sleep/resume, and firmware variations.
- Security auditing, fuzzing, sanitizers suitable for this freestanding target, and formal verification.

The next useful test increments are end-to-end interaction tests for ring-3 apps and copy/move/archive round-trips, malformed ELF and user-fault containment tests, allocation-failure recovery, repeated scheduler stress, and focused page-table/memory-manager invariants. Tests should be added alongside each new subsystem instead of relying on a single boot marker.
