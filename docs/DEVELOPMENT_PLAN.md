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
- [x] Add the initial per-process user address-space path and bounded ELF page mappings with write/execute permissions.
- [x] Validate user output buffers at the initial syscall boundary.
- [ ] Expand page-fault diagnostics, invalid-mapping tests, allocation-failure cleanup, and adversarial isolation coverage.
- [ ] Add demand paging and mature virtual-memory policy.

### M4 — User mode and syscall boundary (early execution path implemented)
- [x] Enter ring 3 from a loaded static ELF64 `ET_EXEC` image.
- [x] Build a bounded initial stack with `argc`, `argv`, empty `envp`, and an `AT_NULL` terminator.
- [x] Provide bring-up syscalls for process exit, serial output, and terminal output through `int 0x80`.
- [x] Validate syscall context and user output-buffer ranges for the implemented write calls.
- [x] Exercise the FAT32-loaded ELF and quoted argument path in QEMU CI.
- [ ] Define a versioned/stable syscall ABI and error-code contract.
- [ ] Add comprehensive user-fault containment and malformed pointer/ELF tests.
- [ ] Add per-process file descriptors and user-mode file I/O.
- [ ] Support `ET_DYN`, dynamic linking, and shared libraries.

### M5 — Storage and services
- [x] Document the current VFS/FAT32 boundary and unsupported writes.
- [x] Exercise FAT32-backed executable loading using a disposable CI disk image.
- [ ] Expand FAT32 read tests against disposable disk fixtures.
- [ ] Implement file/directory creation and writes; test persistence across reboot.
- [ ] Add corruption-safe tests and consistent error propagation.

### M5.5 — User-space desktop application platform

- [ ] Define and implement a bounded, versioned GUI syscall ABI for window lifecycle, draw submission, and queued input/focus events.
- [ ] Keep framebuffer access and z-order under a kernel-owned compositor; never expose kernel pointers to applications.
- [ ] Add QEMU tests for malformed GUI requests, invalid user pointers, focus/input routing, and cleanup when a client exits.
- [ ] Implement durable FAT32-backed VFS create/write operations and safe user-mode write syscalls; verify config persistence across reboot.
- [ ] Migrate Settings to a static ELF64 user-mode app first, with a versioned local config and functional navigation.
- [ ] Migrate File Explorer to user mode and support launching only validated ELF64 executables or documented application manifests.
- [ ] Move Terminal's window, input, and presentation onto the same GUI API while keeping its shell in ring 3.
- [ ] Implement RTC UTC/local policy and a timezone-rule service before claiming region-based local time or DST correctness.

### M6 — Daily-driver prototype
- [ ] Networking, audio, practical USB, and reliable graphics/input.
- [ ] Package installation, signed updates, recovery, and rollback.
- [ ] Security model, permissions, app APIs, and real-hardware testing.

## Latest status

- Backup branch: `backup/pre-desktop-reliability-2026-10-15`.
- Default branch: `master`.
- Current user-mode status: static ELF64 execution from FAT32, bounded `argc`/`argv`, and basic output/exit syscalls are implemented and covered by the pull-request QEMU/UEFI regression workflow. This is not yet a general application environment.
- Current next milestone: establish the user-space GUI syscall/event/presentation boundary. Existing Terminal syscalls are not sufficient for an isolated graphical app. Durable settings also depend on currently unsupported FAT32 create/write operations. See [User-space desktop applications](USERSPACE_GUI.md). Continue merging only after relevant QEMU and UEFI CI checks pass.
