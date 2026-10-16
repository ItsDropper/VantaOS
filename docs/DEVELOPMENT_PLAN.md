# VantaOS Reliability Roadmap

This roadmap prioritizes a trustworthy kernel over visual features. A Windows alternative is a multi-year goal, not a delivery guarantee.

## Engineering rules

- Work on a feature branch and preserve the default branch and dated backup.
- Make small, reviewable changes.
- A successful compile is not proof of a successful boot.
- Record emulator evidence separately from compilation evidence.
- Do not claim user-mode isolation until privilege transitions and per-process address-space permissions are implemented and tested.
- If a test cannot run, mark it NOT RUN.
- Add success markers only after their assertions pass.

## Milestones

### M0 — Reproducible baseline
- [x] Build the kernel and ISO from a clean GitHub Actions checkout.
- [x] Boot the ISO in headless QEMU and capture serial output.
- [ ] Record compiler, assembler, linker, GRUB, and QEMU versions.
- [ ] Capture visual desktop and known crash/reboot baselines.

### M1 — Build and boot regression checks
- [x] Add CI for kernel, ISO, and disk-image creation.
- [x] Capture serial output and assert boot/process lifecycle markers.
- [x] Run QEMU smoke tests at 256 MiB and 512 MiB.
- [x] Document build, architecture, test coverage, and hardware limitations.
- [x] Split build source lists into subsystem Makefiles.
- [x] Move source code into architecture, kernel, memory, drivers, and filesystem directories.

### M1.5 — Independent UEFI boot path (implemented; further hardening planned)
- [x] Define a versioned boot-info ABI and normalize boot information for the physical memory manager; migration of every legacy consumer remains incomplete.
- [x] Keep GRUB/Multiboot as a working fallback and add a separate UEFI x86-64 entry.
- [x] Load ELF64 kernel segments, acquire the final UEFI memory map, and call `ExitBootServices` in the tested OVMF path.
- [x] Add OVMF/QEMU CI for the UEFI loader while retaining GRUB as the fallback.
- [ ] Expand malformed-ELF, memory-map boundary, and missing optional firmware-data tests.

### M2 — Process and scheduler correctness
- [x] Test selected process API guards and lifecycle transitions.
- [x] Verify a returning kernel process is reaped.
- [x] Exercise concurrent scheduler work, remote wakeups, and process migration in the multi-vCPU QEMU regression matrix.
- [ ] Add interrupt-frame layout and stack-bound tests.
- [ ] Expand sustained scheduling and timer-preemption stress coverage.

### M3 — Memory safety and address spaces
- [ ] Add bounds/overflow checks to mapping and frame calculations.
- [ ] Implement per-process page tables with correct kernel/user permissions.
- [ ] Add page-fault diagnostics and invalid-mapping tests.

### M4 — User mode and syscall boundary
- [ ] Define a versioned syscall ABI.
- [ ] Add controlled privilege transitions and per-process user stacks.
- [ ] Validate syscall numbers and user pointers.
- [ ] Contain a user-process fault without taking down the kernel.

### M5 — Storage and services
- [x] Document the current VFS/FAT32 boundary and unsupported writes.
- [ ] Test FAT32 reads against disposable disk fixtures.
- [ ] Implement file/directory creation and writes; test persistence across reboot.
- [ ] Add corruption-safe tests and consistent error propagation.

### M6 — Daily-driver prototype
- [ ] Networking, audio, practical USB, and reliable graphics/input.
- [ ] Package installation, signed updates, recovery, and rollback.
- [ ] Security model, permissions, app APIs, and real-hardware testing.

## Latest status

- Backup branch: `backup/pre-desktop-reliability-2026-10-15`.
- Default branch: `master`.
- Current work moves source into Linux-inspired top-level subsystem directories and updates manifests/docs. Merge only after clean build and QEMU CI pass.
