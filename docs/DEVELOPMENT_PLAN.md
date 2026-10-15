# VantaOS Reliability Roadmap

This roadmap prioritizes a trustworthy kernel over visual features. A Windows alternative is a multi-year goal, not a delivery guarantee.

## Engineering rules

- Work on a feature branch; keep `main` and the dated backup branch untouched until changes are reviewed.
- Make the smallest change that addresses a documented failure mode.
- Build after each logical change. A successful compile is not proof of a successful boot.
- Record emulator boot evidence separately from compilation evidence.
- Do not claim user-mode isolation until privilege transitions and per-process address-space permissions are implemented and tested.
- Do not claim hardware support based only on QEMU or driver metadata.
- If a test cannot be run, mark it **not run**, not passed.

## Milestones

### M0 — Reproducible baseline
- [ ] Record compiler, assembler, linker, GRUB, and QEMU versions.
- [ ] Build a clean kernel and ISO from a fresh checkout.
- [ ] Boot the ISO in QEMU and record visible boot evidence.
- [ ] Capture current process/scheduler behavior and known crash/reboot cases.

**Exit criteria:** repeatable clean build and recorded boot evidence.

### M1 — Build and boot regression checks
- [x] Add CI that builds the kernel and creates the ISO and disk image.
- [x] Run the checks on pushes and pull requests.
- [x] Add serial output and QEMU assertions for successful OS initialization and process lifecycle checks.

**Exit criteria:** clean build runs on pushes and pull requests; boot tests have observable pass/fail conditions.

### M2 — Process and scheduler correctness
- [x] Validate process-name termination for overlong names in the CI-only QEMU probe.
- [x] Exercise invalid process API inputs in the CI-only QEMU probe.
- [ ] Add dedicated automated checks for interrupt-frame layout and stack bounds.
- [x] Verify a returning kernel process can be preempted and reaped in the CI-only QEMU probe.
- [ ] Verify all process state transitions, not only invalid IDs.
- [ ] Test timer preemption repeatedly without unexplained resets.

**Exit criteria:** lifecycle/state tests pass and QEMU demonstrates repeated scheduling without panic or reboot.

### M3 — Memory safety and address spaces
- [ ] Document identity/high-memory mappings and physical-memory assumptions.
- [ ] Add bounds/overflow checks around mapping and frame calculations.
- [ ] Implement per-process page tables with correct kernel/user permissions.
- [ ] Add page-fault diagnostics and invalid-mapping tests.

**Exit criteria:** an actual user-mode test proves a process cannot access another process's private memory. Current kernel processes sharing one address space do not meet this criterion.

### M4 — User mode and syscall boundary
- [ ] Define a versioned syscall ABI.
- [ ] Add controlled privilege transitions and per-process user stacks.
- [ ] Validate syscall numbers and user pointers.
- [ ] Contain a deliberate user-process fault without taking down the kernel.

**Exit criteria:** multiple unprivileged test programs run, use validated syscalls, and one can fail without crashing the kernel or another process.

### M5 — Storage and services
- [ ] Separate VFS contracts from device/filesystem implementations.
- [ ] Test FAT32 reads/writes against a disposable image.
- [ ] Add consistent error propagation and corruption-safe tests.
- [ ] Implement process-safe service interfaces.

**Exit criteria:** persistence tests pass against a fresh disk image and a second boot reads back written data.

### M6 — Daily-driver prototype
- [ ] Reliable input, graphics, storage, and networking.
- [ ] Audio and practical USB support.
- [ ] Package installation, signed updates, recovery, and rollback.
- [ ] Security model, permissions, documentation, and app APIs.
- [ ] Test on real hardware without risking the host OS or personal data.

**Exit criteria:** a defined workload can be used repeatedly with documented hardware compatibility and recovery behavior.

## Status at roadmap creation

- Backup branch: `backup/pre-desktop-reliability-2026-10-15`.
- Work branch: `work/kernel-reliability-2026-10-15`.
- Three code-level fixes are on the work branch: process-name copying avoids an unsequenced index read/write, the process-return trampoline leaves timer interrupts enabled so the scheduler can run, and stack validation accepts the actual same-privilege long-mode IRQ frame instead of assuming RSP/SS are pushed on every interrupt.
- The branch currently passes a clean CI build and QEMU boot/lifecycle assertions, including process API guards, bounded process-name handling, scheduling a kernel process, and reaping it after return.
- This does **not** establish user-mode isolation, process memory isolation, or reliable behavior on physical hardware. Those remain unverified.
