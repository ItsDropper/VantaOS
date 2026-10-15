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
- Add a test marker only after its assertions have passed; CI rejects failure markers and missing success markers.

## Milestones

### M0 — Reproducible baseline
- [ ] Record compiler, assembler, linker, GRUB, and QEMU versions.
- [x] Build the kernel and ISO from a clean GitHub Actions checkout.
- [x] Boot the ISO in headless QEMU and capture serial output.
- [ ] Capture a visual desktop baseline and known crash/reboot cases.

**Exit criteria:** repeatable clean build and recorded boot evidence.

### M1 — Build and boot regression checks
- [x] Add CI that builds the kernel and creates the ISO and disk image.
- [x] Run the checks on pushes and pull requests.
- [x] Add serial output and QEMU assertions for OS initialization and process lifecycle checks.
- [x] Run the QEMU smoke suite with 256 MiB and 512 MiB guest RAM.
- [x] Document test commands, coverage, architecture, and hardware limitations.

**Exit criteria:** clean build runs on pushes and pull requests; boot tests have observable pass/fail conditions.

### M2 — Process and scheduler correctness
- [x] Validate process-name termination for overlong names in the CI-only QEMU probe.
- [x] Exercise invalid process API inputs in the CI-only QEMU probe.
- [ ] Add dedicated automated checks for interrupt-frame layout and stack bounds.
- [x] Verify a returning kernel process can be scheduled and reaped in the CI-only QEMU probe.
- [x] Exercise selected ready/running/sleeping/wake/termination transitions.
- [ ] Stress timer preemption and repeated scheduling for longer periods without unexplained resets.

**Exit criteria:** lifecycle/state tests pass and QEMU demonstrates repeated scheduling without panic or reboot.

### M3 — Memory safety and address spaces
- [x] Document identity/high-memory mapping assumptions and the current limitations.
- [ ] Add bounds/overflow checks around mapping and frame calculations.
- [ ] Implement per-process page tables with correct kernel/user permissions.
- [ ] Add page-fault diagnostics and invalid-mapping tests.

**Exit criteria:** an actual user-mode test proves a process cannot access another process's private memory. Current kernel processes sharing kernel-mode execution do not meet this criterion.

### M4 — User mode and syscall boundary
- [ ] Define a versioned syscall ABI.
- [ ] Add controlled privilege transitions and per-process user stacks.
- [ ] Validate syscall numbers and user pointers.
- [ ] Contain a deliberate user-process fault without taking down the kernel.

**Exit criteria:** multiple unprivileged test programs run, use validated syscalls, and one can fail without crashing the kernel or another process.

### M5 — Storage and services
- [x] Document the current VFS/FAT32 boundary and explicitly identify unsupported create/write operations.
- [ ] Separate VFS contracts from device/filesystem implementations.
- [ ] Test FAT32 reads against disposable, controlled disk fixtures.
- [ ] Implement file/directory creation and writes, then test persistence across a second boot.
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

## Latest status

- Preserved backup branch: `backup/pre-desktop-reliability-2026-10-15`.
- Previous reliability work merged to `main`; post-merge CI passed at [run 38789347032](https://github.com/ItsDropper/VantaOS/actions/runs/38789347032).
- Current follow-up branch: `work/comprehensive-tests-docs-2026-10-15`.
- This branch adds process-state checks, filesystem namespace and invalid VFS-handle checks, multi-memory QEMU boots, a README, and focused documentation.
- The new branch must pass its own CI before it is merged. Until that happens, these latest additions are **pending verification**.
- None of these checks establishes user-mode isolation, process memory isolation, complete filesystem write support, or reliable behavior on physical hardware.
