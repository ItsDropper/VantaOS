# Testing and verification

This document records what the current automation actually checks. A green workflow means the listed assertions passed in its tested environment; it does not prove every code path, all hardware configurations, or security.

## CI workflow

Workflow: [`.github/workflows/build.yml`](../.github/workflows/build.yml)

The workflow runs on pull requests, manual dispatch, and pushes to the branches explicitly listed in the workflow file. It uses an Ubuntu GitHub-hosted runner and:

1. Installs GCC/build tools, NASM, GRUB utilities, FAT tools, QEMU, GNU-EFI, OVMF, and GPT tooling.
2. Runs `make list-sources` and verifies expected subsystem manifests.
3. Builds the kernel, ISO, and FAT32 development image with `VANTAOS_SELF_TEST=1`.
4. Checks that `kernel.bin`, `VantaOS.iso`, and `vantaos.img` are non-empty.
5. Boots the GRUB ISO in headless QEMU and validates serial markers while requiring QEMU to remain alive through the smoke-test timeout.
6. Runs the following GRUB/QEMU matrix:

| Guest RAM | vCPUs | Additional requirement |
| ---: | ---: | --- |
| 256 MiB | 1 | Required boot and regression markers |
| 512 MiB | 2 | Required markers plus SMP scheduler stress, remote wake, and migration |
| 512 MiB | 4 | Required markers plus SMP scheduler stress, remote wake, and migration |

7. Builds the UEFI image and boots it under OVMF, requiring loader, ELF load, `ExitBootServices`, kernel handoff, boot-info, APIC/SMP, and memory-manager markers.

The workflow's YAML is the source of truth if this summary and the implementation ever differ.

## Marker contract

The workflow checks for the required markers listed in `.github/workflows/build.yml`. Some key markers are:

| Marker | Evidence |
| --- | --- |
| `VANTAOS_BOOT_OK` | Kernel initialization reached the OS run path. |
| `VANTAOS_BOOT_INFO_READY` | Boot information was normalized into the versioned contract. |
| `VANTAOS_APIC_LOCAL_OK` / `VANTAOS_SMP_TOPOLOGY_OK` | APIC-local and CPU-topology checks passed. |
| `VANTAOS_SMP_AP_START_OK` | Application processors reached the tested startup checkpoint. |
| `VANTAOS_SMP_LOCAL_TIMER_OK` | The tested per-CPU timer path passed. |
| `VANTAOS_SMP_RESCHEDULE_IPI_OK` / `VANTAOS_SMP_RESCHEDULE_PENDING_OK` | Reschedule IPI and pending-request assertions passed. |
| `VANTAOS_SMP_IDLE_CONTEXTS_OK` | Per-CPU idle-context assertions passed. |
| `VANTAOS_TLB_SHOOTDOWN_OK` | The tested cross-CPU TLB shootdown assertions passed. |
| `VANTAOS_PMM_ALLOCATOR_OK` / `VANTAOS_HEAP_ALLOCATOR_OK` | Selected physical-frame and heap allocation/free invariants passed. |
| `VANTAOS_PROCESS_STATE_TRANSITIONS_OK` / `VANTAOS_SCHEDULER_REAP_OK` | Selected kernel-process lifecycle and reaping assertions passed. |
| `VANTAOS_SMP_SCHEDULER_STRESS_OK` | Multi-vCPU stress assertions for task completion, CPU participation, duplicate-entry detection, and measured overlap passed. |
| `VANTAOS_SMP_REMOTE_WAKE_OK` | The tested remote wakeup path passed. |
| `VANTAOS_SMP_MIGRATION_OK` | The tested cross-CPU migration path passed. |
| `VANTAOS_UEFI_EXIT_BOOT_SERVICES_OK` / `VANTAOS_UEFI_KERNEL_HANDOFF` | UEFI loader reached the tested exit/handoff checkpoints. |

A marker is evidence for the assertions tied to it only. It is not a proof of general correctness. A success marker must never be printed before its assertions pass. The workflow rejects `VANTAOS_*_FAIL` markers.

## Run locally

Install the packages listed in the root [README](../README.md), then:

```sh
VANTAOS_SELF_TEST=1 ./build.sh
```

A basic single-vCPU serial smoke test can be run with:

```sh
timeout 12s qemu-system-x86_64 \
  -machine pc -smp 1 -m 256M \
  -drive file=VantaOS.iso,media=cdrom,format=raw \
  -drive file=vantaos.img,if=ide,format=raw \
  -boot order=d -display none \
  -serial file:boot.log -monitor none -no-reboot
```

For this smoke test, exit code 124 from `timeout` is expected: QEMU should remain alive for the full interval. Inspect `boot.log` for missing or failure markers. This command is not a substitute for the full CI matrix.

## UEFI path

The UEFI image contains `EFI/BOOT/BOOTX64.EFI` and `EFI/VANTA/KERNEL.ELF`. The OVMF regression checks the loader's tested ELF-load and boot-services handoff path and verifies selected kernel markers after entry.

The UEFI path is exercised in CI, but that does not establish compatibility with arbitrary firmware, secure boot, or physical hardware. Malformed-ELF fuzzing and broader firmware variation remain future work.

## Coverage gaps

Current CI does **not** establish:

- Exhaustive scheduling interleavings, race freedom, long-duration SMP stability, or correctness under arbitrary interrupt timing.
- A user-mode privilege boundary, secure process isolation, syscall safety, or complete page-table permission enforcement.
- Exhaustive malformed-memory-map fuzzing, allocator fragmentation behavior, or recovery under sustained memory pressure.
- General persistent FAT32 create/write support, filesystem corruption handling, crash consistency, or power-loss recovery.
- Pixel-level desktop correctness, scripted user interaction, broad display/input support, or visual regression.
- Real-device compatibility, broad USB/HID behavior, Wi-Fi, Ethernet, audio, modern GPU acceleration, sleep/resume, or firmware variation.
- Independent security audit, formal verification, or exhaustive fuzzing.

Useful next increments are repeated scheduler stress, interrupt-frame/stack-bound invariants, page-table permission tests, disposable FAT32 fixture tests, and automated UI/input checks. Add focused tests alongside the subsystem changes they protect.
