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
- [x] Add bounded per-process file descriptors and user-mode file I/O for the supported VFS/FAT32 operations.
- [ ] Support `ET_DYN`, dynamic linking, and shared libraries.

### M5 — Storage and services
- [x] Document the current VFS/FAT32 boundary and unsupported writes.
- [x] Exercise FAT32-backed executable loading using a disposable CI disk image.
- [ ] Expand FAT32 read tests against disposable disk fixtures.
- [x] Validate bounded FAT32 8.3 regular-file writes and VFS readback in disposable QEMU disk fixtures.
- [ ] Implement long-filename writes, directory creation, rename/delete, crash-safe replacement, and broader persistence/corruption tests.
- [ ] Add corruption-safe tests and consistent error propagation.

### M5.5 — User-space desktop application platform

- [x] Define and implement the initial bounded GUI syscall ABI for window lifecycle, draw submission, and queued input/focus/close events.
- [x] Keep framebuffer access and z-order under the kernel-owned renderer; do not expose kernel pointers to applications.
- [x] Add QEMU tests for malformed GUI requests, invalid user pointers, and cleanup when a client exits.
- [x] Implement bounded FAT32-backed regular-file create/write and user-mode write syscalls; verify the supported persistence path across a second boot.
- [ ] Expand GUI regression coverage for click routing, focus transitions, queue overflow, close requests, and invalid event buffers.
- [x] Add an initial static ELF64 ring-3 Settings app using the GUI ABI, with app-local accent/compact-row preferences and a FAT32-backed config file. Global desktop theming and broader malformed-config/error-path coverage remain incomplete.
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
- Current user-mode status: static ELF64 execution, bounded process/file syscalls, and an initial process-owned GUI ABI are implemented. CI checks selected syscall, file-I/O, multi-cluster FAT32, GUI malformed-request, dead-window-cleanup, and second-boot persistence cases. This remains an early bring-up environment, not a stable application platform.
- Current next milestone: expand GUI event/focus/input tests, then harden Settings configuration parsing and I/O failure behavior. The Settings app is an initial ring-3 ELF, but its preferences are app-local rather than global desktop settings. Date/time settings must wait for an explicit RTC policy and tested timezone conversion. See [User-space desktop applications](USERSPACE_GUI.md).
