# Testing and verification

This document distinguishes what automation actually checks from what is still untested. A green CI run means the listed assertions passed; it does not prove every code path or every hardware configuration works.

## Continuous integration

Workflow: [`.github/workflows/build.yml`](../.github/workflows/build.yml)

For pushes to `main`, pull requests, and manual workflow runs, GitHub Actions:

1. Checks out the repository on a clean Ubuntu runner.
2. Installs GCC/build tools, NASM, GRUB utilities, FAT tools, and QEMU.
3. Builds the freestanding kernel, bootable ISO, and FAT32 development image with `VANTAOS_SELF_TEST=1`.
4. Asserts that `kernel.bin`, `VantaOS.iso`, and `vantaos.img` exist and are non-empty.
5. Boots the ISO in headless QEMU with serial output captured to a log.
6. Requires all success markers, rejects any `VANTAOS_*_FAIL` marker, and requires QEMU to remain running until the smoke-test timeout.
7. Repeats the QEMU boot at 256 MiB and 512 MiB guest RAM. CI waits briefly after QEMU exits so the serial log is flushed before assertions run.

## Runtime assertions

| Marker | What it establishes |
| --- | --- |
| `VANTAOS_BOOT_INFO_ADAPTER_OK` | The versioned boot-info header has the expected magic/version/size; Multiboot memory ranges and framebuffer fields normalize correctly; malformed map entries and null arguments are rejected; and the live GRUB handoff was normalized. |

| Marker | What it establishes |
| --- | --- |
| `VANTAOS_PMM_ALLOCATOR_OK` | The physical allocator is initialized; two distinct aligned frames are allocated; invalid and duplicate frees are rejected; and free/used accounting returns to baseline. |
| `VANTAOS_HEAP_ALLOCATOR_OK` | The heap is initialized; aligned allocations preserve test data; invalid and duplicate frees are rejected; and usage accounting returns to baseline. |
| `VANTAOS_PROCESS_API_GUARDS_OK` | Selected invalid process API arguments are rejected. |
| `VANTAOS_PROCESS_NAME_OK` | An overlong process name remains null-terminated at the maximum name index. |
| `VANTAOS_PROCESS_STATE_TRANSITIONS_OK` | A test process transitions through ready, running, sleeping, wake-up, and termination/reaping paths. |
| `VANTAOS_FILESYSTEM_NAMESPACE_OK` | The filesystem/VFS initializes, root and standard directories can be resolved, a missing path stays missing, and root listing returns without error. |
| `VANTAOS_VFS_HANDLE_GUARDS_OK` | Invalid VFS read/write/close handles are rejected. |
| `VANTAOS_SCHEDULER_PROBE_OK` | The scheduler runs the dedicated kernel-mode probe entry. |
| `VANTAOS_SCHEDULER_REAP_OK` | The observer sees the completed probe process removed from the process table. |

These tests run inside the kernel before/after enabling interrupts as appropriate. They do **not** run untrusted user-mode programs.

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

## What is not yet covered

- Exhaustive process-state combinations, sustained preemption stress, race conditions, or long-duration stability.
- Page-table permission enforcement, user/supervisor transitions, syscall validation, and process-to-process memory isolation.
- Physical-memory-map malformed-entry/overflow fuzzing, allocator fragmentation, allocation-failure recovery, and long-duration memory-pressure tests.
- Actual FAT32 write/create support; the current filesystem write/create functions report unsupported.
- Filesystem corruption, power-loss recovery, and persistent-data round trips.
- Pixel-level desktop correctness, input interaction scripts, display modes, and visual regression tests.
- Real-device compatibility, USB/HID edge cases, NICs, Wi-Fi, audio, GPU acceleration, sleep/resume, and firmware variations.
- Security auditing, fuzzing, sanitizers suitable for this freestanding target, and formal verification.

The next useful test increments are a repeated scheduler stress scenario, focused page-table/memory-manager invariants, FAT32 read-only fixture tests, and automated input/graphics checks. Tests should be added alongside each new subsystem instead of relying on a single boot marker.
