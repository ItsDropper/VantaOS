# Anti-cheat and Windows game compatibility plan

Copyright (c) 2026 ItsDropper

This is a long-term research and engineering plan, not a statement that Easy Anti-Cheat (EAC), Windows games, or kernel anti-cheat currently work on VantaOS. The initial target for investigation is EAC because some games already use its supported Linux/Wine/Proton integration path. That does not mean every EAC-enabled game supports Linux, Wine, Proton, or VantaOS.

## Goal and non-goals

**Goal:** establish a legitimate path for running games whose publishers and anti-cheat vendors explicitly support the chosen environment, without weakening the OS security model or pretending an unsupported platform is trusted.

**Non-goals:**
- Do not load a Windows kernel driver into VantaOS or pretend a Windows kernel ABI exists.
- Do not spoof Secure Boot, TPM measurements, kernel identity, signatures, or integrity state.
- Do not bypass anti-cheat checks, conceal a compatibility layer, or defeat publisher policy.
- Do not claim compatibility based on a launcher opening, a driver loading, or a game reaching its menu.
- Do not add game-specific behavior to the kernel. Compatibility belongs in userspace or an explicitly supported vendor integration.

## Two routes to investigate

### Route A — userspace compatibility runtime

Investigate the minimum user-mode Windows API and runtime surface needed by real, explicitly supported games and their official EAC integration. Treat this as a large compatibility project, not a thin wrapper around a few DLL names.

- Start with executable formats, process/thread lifecycle, virtual memory, synchronization, file and device I/O, clocks, dynamic loading, exception behavior, and graphics/audio APIs.
- Define a userspace compatibility boundary. Keep Windows semantics in a userspace runtime; keep the kernel's native syscall ABI generic.
- Add compatibility only when a concrete application/test requires it, with API contract tests and explicit unsupported behavior.
- Evaluate whether a maintained upstream compatibility stack can be ported or reused before designing a replacement. Track licensing, dependency, ABI, and maintenance costs.
- Do not implement EAC-specific stubs that report false support. EAC must use a vendor-approved integration and real security capabilities.

### Route B — native security and vendor integration

Build the security primitives and platform evidence that a vendor may require, then investigate an official EAC integration with Epic and the game publisher.

- Establish a trustworthy boot chain and a defined way to verify boot artifacts.
- Design signed kernel/module policy and verifiable system-integrity reporting. Reports must describe actual measurements and validation, not user-controlled claims.
- Strengthen process/address-space isolation, executable loading, privilege boundaries, and privileged API auditing.
- Document key ownership, update/revocation behavior, recovery behavior, and failure-closed semantics for any signing or integrity scheme.
- Confirm the vendor's actual requirements before selecting Secure Boot, TPM-backed measurements, attestation, or other mechanisms as implementation requirements. A feature's presence alone does not establish vendor trust.

Routes A and B are complementary. A userspace compatibility runtime does not replace platform security, and platform security does not automatically make a Windows-only anti-cheat driver compatible.

## Repository audit: current baseline

The repository currently provides an early foundation, not a stable Windows compatibility platform:

- Static ELF64 ET_EXEC applications run at x86-64 ring 3 in private address spaces. The loader bounds load segments, zero-fills tails, rejects writable/executable segments and overlapping pages, and uses a non-executable user stack.
- The public VantaOS syscall ABI is project-specific and currently versioned by VANTA_USER_ABI_VERSION (version 10). The docs explicitly say it is an initial ABI, not a stable long-term or POSIX contract.
- Process creation, child spawn/wait, bounded file descriptors, IPC, and a project-specific GUI boundary exist. The userspace environment remains freestanding: no complete libc, dynamic linker, ET_DYN, shared libraries, mature thread runtime, or POSIX process environment.
- The kernel uses its own process and memory-management contracts. These are not Win32, NT, Linux, or Wine-compatible interfaces.
- Signed boot artifacts, an enforced secure-boot chain, TPM-backed measurements/attestation, and vendor-approved EAC support are not established by this plan and must not be described as implemented.
- GPU acceleration, graphics API compatibility, audio, input, timing, and broad physical-hardware support are independent prerequisites for serious games.

Treat these bullets as the audit baseline at the time this plan was written. Before implementing each milestone, re-fetch the relevant source and tests from the current branch and revise the status from evidence.

## Ordered milestones and acceptance gates

### A0 — source and vendor-requirement audit

- [ ] Inventory the process, scheduler, address-space, ELF loader, syscall, VFS, GUI, graphics, audio, and driver interfaces, including tests and untested paths.
- [ ] Separate current implementation facts from intended contracts and gaps.
- [ ] Research current official EAC Linux/Wine/Proton support and the requirements of a specific game; identify the publisher-controlled enablement step.
- [ ] Record vendor-confirmed security/attestation requirements before choosing a boot-integrity design.
- **Exit gate:** a source-linked gap matrix and a concrete, explicitly supported target scenario; no claim of compatibility.

### A1 — stabilize native userspace contracts

- [ ] Define syscall ABI compatibility policy: versioning, reserved numbers, argument/return conventions, error codes, feature discovery, and deprecation rules.
- [x] Add a static checker that compares all public syscall numbers with kernel definitions and dispatch cases; CI wiring is committed, but the checker has not yet been observed passing in a workflow run.
- [ ] Expand adversarial tests for invalid user pointers, integer overflow, malformed ELF segments, allocation failure, process exit during IPC/I/O, and address-space cleanup.
- [ ] Define reusable native interfaces for process/thread lifecycle, virtual memory, file descriptors, clocks, synchronization, and dynamic loading only as implementation/test requirements become concrete.
- **Exit gate:** documented contracts and passing tests for the supported native ABI. Do not call the ABI stable until a deliberate compatibility policy and migration story exist.

### A2 — runtime and application feasibility

- [ ] Select a minimal, real userspace application as the first compatibility target; prefer a small conformance harness over an entire game.
- [ ] Determine whether a maintained upstream Windows compatibility runtime can be ported; estimate ABI, libc, loader, syscall, graphics, and licensing requirements.
- [ ] Implement the runtime in userspace, keeping kernel interfaces generic.
- [ ] Build a conformance suite for each implemented API and fail explicitly for unsupported behavior.
- **Exit gate:** repeatable execution of a declared test suite, with no stubs falsely claiming successful semantics.

### A3 — verifiable platform integrity

- [ ] Specify the threat model and trusted root for boot verification.
- [ ] Define signed artifact formats, key provisioning, rotation/revocation, recovery, and update behavior before enforcing a signature policy.
- [ ] Implement measured/verified boot only against documented firmware and bootloader capabilities.
- [ ] Expose integrity state through a narrow, permission-controlled, auditable interface; do not expose signing secrets or permit userspace to assert its own trust.
- [ ] Test modified, unsigned, revoked, malformed, and unavailable-artifact failure cases.
- **Exit gate:** reproducible negative and positive tests on a defined boot configuration. TPM or Secure Boot presence alone is not proof of EAC acceptance.

### A4 — official EAC integration investigation

- [ ] Identify one EAC-enabled game whose publisher explicitly supports Linux/Wine/Proton or another vendor-approved environment.
- [ ] Confirm that the game's publisher has enabled that support and that the chosen runtime/OS configuration is in the supported matrix.
- [ ] Contact Epic and the publisher for any VantaOS-specific integration or attestation requirements.
- [ ] Implement only documented, authorized integration requirements and preserve the vendor's integrity model.
- **Exit gate:** written/official support confirmation plus end-to-end evidence from a supported game and anti-cheat session. Otherwise report as unsupported or unverified.

### A5 — end-to-end game validation

- [ ] Validate boot, runtime startup, GPU/API initialization, audio, input, networking, suspend/recovery behavior, and anti-cheat session independently.
- [ ] Test clean startup, normal shutdown, update, integrity-failure, and unsupported-environment paths.
- [ ] Publish a compatibility matrix naming exact game, runtime, graphics stack, firmware configuration, and test evidence.
- **Exit gate:** a repeatable supported configuration. Passing one game does not imply general EAC compatibility.

## Verification and reporting rules

For every feature, report one of: **planned**, **implemented**, **exercised**, **verified**, or **supported**, following the repository engineering model. Cite the exact test and configuration when claiming verification. If CI has no run or a test cannot be executed, say so. Do not claim a vendor integration works until the vendor/publisher-supported end-to-end gate is met.

## Immediate next engineering task

Continue the source-linked audit and harden the native userspace ABI first. The most useful first implementation is automated ABI consistency checking plus targeted negative tests, not a fake EAC wrapper. Keep the Vanta Launcher compatibility work tracked separately: it shares the need for a robust native userspace runtime but does not itself establish anti-cheat support.
