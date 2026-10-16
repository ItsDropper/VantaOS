# VantaOS reliability roadmap

This roadmap prioritizes kernel correctness, reproducibility, and explicit trust boundaries before feature breadth. A daily-driver OS is a long-term goal, not a delivery guarantee.

## Engineering rules

- Work on a feature branch and keep changes reviewable.
- A successful compile is not proof of a successful boot.
- Record build, emulator, and physical-hardware evidence separately.
- Do not claim user-mode isolation until privilege transitions and per-process permissions are implemented and tested.
- Mark tests that did not run as **NOT RUN**.
- Emit success markers only after their assertions pass.
- Update the architecture docs and ADRs when subsystem contracts or durable design choices change.

## Milestones

### M0 — Reproducible baseline
- [x] Build the kernel and ISO from a clean GitHub Actions checkout.
- [x] Boot the ISO in headless QEMU and capture serial output.
- [ ] Record compiler, assembler, linker, GRUB, and QEMU versions.
- [ ] Capture visual desktop and known crash/reboot baselines.

### M1 — Build and boot regression checks
- [x] Add CI for kernel, ISO, and development disk-image creation.
- [x] Assert boot, memory, filesystem, and selected process lifecycle markers.
- [x] Run GRUB/QEMU smoke tests at multiple memory sizes.
- [x] Split source lists into subsystem Makefiles.
- [x] Organize source into architecture, kernel, memory, drivers, filesystem, and OS-facing areas.
- [x] Document build, architecture, test coverage, and hardware limitations.

### M1.5 — Independent UEFI boot path
- [x] Define a versioned boot-information contract independent of direct loader-specific structures.
- [x] Keep the GRUB/Multiboot path available while adding a separate UEFI x86-64 entry.
- [x] Load the ELF64 kernel and exercise the UEFI boot-services handoff.
- [x] Add OVMF/QEMU CI for the loader and kernel handoff.
- [ ] Add malformed-ELF regression cases and broader firmware variation tests.

### M2 — Process and SMP scheduler correctness
- [x] Test selected process API guards and lifecycle transitions.
- [x] Verify that a returning kernel process is reaped.
- [x] Exercise AP startup, per-CPU state, local timers, reschedule IPIs, and idle contexts in QEMU.
- [x] Test remote wakeups and process migration in multi-vCPU QEMU.
- [x] Add scheduler stress assertions for completion, CPU participation, duplicate process-entry detection, and concurrent overlap.
- [ ] Add interrupt-frame layout and stack-bound tests.
- [ ] Expand repeated scheduling/preemption stress and long-duration SMP testing.

### M3 — Memory safety and address spaces
- [x] Add selected physical allocator, heap, normalized boot-info, and TLB-shootdown checks.
- [ ] Expand bounds/overflow checks and malformed memory-map testing.
- [ ] Implement per-process page tables with correct kernel/user permissions.
- [ ] Add page-fault diagnostics and invalid-mapping tests.
- [ ] Test allocation-failure recovery and memory-pressure behavior.

### M4 — User mode and syscall boundary
- [ ] Define a versioned syscall ABI.
- [ ] Add controlled privilege transitions and per-process user stacks.
- [ ] Validate syscall numbers and user pointers.
- [ ] Contain a user-process fault without taking down the kernel.
- [ ] Demonstrate process-to-process isolation with adversarial tests.

### M5 — Storage and services
- [x] Document the current VFS/FAT32 boundary and unsupported writes.
- [ ] Test FAT32 reads against disposable disk fixtures.
- [ ] Implement file/directory creation and writes; test persistence across reboot.
- [ ] Add corruption-safe tests and consistent error propagation.

### M6 — Usable system prototype
- [ ] Establish reliable graphics/input behavior with automated regression coverage.
- [ ] Add practical USB and networking, followed by audio where feasible.
- [ ] Define package installation, signed updates, recovery, and rollback.
- [ ] Establish a security model, application APIs, and repeatable real-hardware testing.

## How to use this roadmap

Checkboxes describe project milestones, not release promises. Keep completed items tied to current evidence; when a test or subsystem regresses, update the status rather than preserving a misleading historical checkmark. The exact automated coverage is maintained in [Testing and verification](TESTING.md).
