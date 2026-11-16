---
name: vantaos-kernel-engineering
description: Use when implementing, debugging, reviewing, or planning VantaOS kernel, architecture, memory management, scheduling, drivers, storage, networking, graphics, boot, or userspace infrastructure. Prioritizes substantive modern-OS engineering over demos and superficial changes.
---

# VantaOS Kernel Engineering

## Mission

Move VantaOS toward a genuinely capable, modern operating system through **real, integrated, maintainable implementations**. Optimize for architectural correctness, measurable capability, reliability, security, and eventual gaming-class workloads—not feature-count inflation, mock demonstrations, or code that merely looks sophisticated.

Work in **long, uninterrupted engineering sessions** when the task warrants it. Own the task from investigation through implementation, integration, verification, and final diff review. Do not stop after producing a plan or a partial scaffold when the requested work can reasonably be completed.

## Non-negotiable engineering principles

1. **Implement the real thing.** Do not substitute placeholders, stubs, fake success paths, hard-coded demo output, simulated hardware behavior, or documentation-only claims for the requested functionality. A narrowly scoped, honest implementation is better than a broad fake one.
2. **Understand before changing.** Inspect the relevant source, call paths, interfaces, build rules, architecture constraints, tests, and existing design records. Identify the root cause and ownership boundary before editing.
3. **Integrate end to end.** New functionality must be wired into initialization, lifecycle, error handling, resource ownership, synchronization, and teardown as applicable. An isolated helper that is never used is not a completed feature.
4. **Preserve architecture.** Follow existing subsystem ownership, naming, formatting, and layering. Do not bypass a subsystem's public interface by reaching into its internals. Update shared contracts deliberately.
5. **Be explicit about maturity.** Distinguish implemented, host-tested, QEMU-tested, hardware-tested, and merely planned. Never claim hardware support or correctness without corresponding evidence.
6. **Prefer correctness over breadth.** Handle failure paths, overflow, bounds, races, lifetime, alignment, privilege transitions, and malformed input. Avoid expanding scope until the core path is sound.
7. **No unverified APIs or assumptions.** Read the actual headers and implementations. Do not invent hardware registers, CPU features, ABI details, toolchain flags, or external API behavior.
8. **Do not weaken tests to get green CI.** Fix the implementation or correct a demonstrably invalid test. Preserve diagnostic markers and only emit success markers after assertions pass.
9. **Keep documentation synchronized with code.** Update contracts and status documents when behavior changes; do not describe planned work as implemented.
10. **Minimize unrelated churn.** Keep changes focused, but do not omit necessary integration or tests merely to keep the diff small.

## Modern operating-system direction

Evaluate work against the requirements of a modern general-purpose OS, with a long-term ambition to support demanding interactive and gaming workloads. Relevant engineering dimensions include:

- SMP correctness, preemption, scheduler latency, CPU topology, affinity, and load balancing.
- Virtual memory, demand paging, memory mapping, copy-on-write, page reclamation, and allocator scalability.
- Interrupt handling, timers, high-resolution time, deferred work, and low-latency synchronization.
- Process isolation, syscall ABI design, capabilities/permissions, executable loading, dynamic linking, and robust userspace.
- DMA-safe device memory, IOMMU-aware design where feasible, PCIe discovery, interrupt remapping, and driver lifecycle.
- Storage queues, asynchronous I/O, caching, filesystem consistency, and crash recovery.
- Network throughput, concurrency, protocol correctness, and secure transport foundations.
- Audio, input, display, GPU command submission, memory management, synchronization, and compositing.
- Observability: tracing, profiling, counters, fault diagnostics, stress tests, and reproducible benchmarks.

These are evaluation dimensions, not permission to bolt on unrelated subsystems. Choose work based on actual repository blockers and dependencies. Do not claim that VantaOS is ready for gaming or production merely because one subsystem has advanced.

## Required workflow for substantial tasks

### 1. Establish repository reality
- Read the relevant skill files and project instructions first.
- Inspect the current branch and working tree when local tools are available; with GitHub-only access, inspect the target branch and current file versions before writing.
- Trace the affected code from entry point through consumers.
- Read relevant architecture docs, decision records, build configuration, CI workflows, and existing tests.
- Identify invariants, current limitations, and the smallest correct integration boundary.

### 2. Form an implementation strategy
- State the concrete defect or missing capability and its root cause.
- Identify dependencies and ordering constraints.
- Define observable acceptance criteria and tests before coding.
- Prefer a coherent end-to-end slice over disconnected changes.

### 3. Implement completely
- Make production-path code changes, not scaffolding.
- Include initialization, synchronization, cleanup, and error handling.
- Add or extend focused tests, including negative and boundary cases.
- Add QEMU or integration coverage when the environment supports it.
- Keep interfaces and documentation aligned.

### 4. Verify iteratively
Run the most relevant checks available, then broader checks when appropriate:
- Build affected targets from a clean or appropriately controlled state.
- Run focused host tests and static architecture/ABI checks.
- Run relevant QEMU boot and regression tests.
- Inspect CI results when available; do not infer them from local success.
- Diagnose failures to root cause and fix them. Do not stop at the first failing run.
- Review the final diff for dead code, accidental scope, unsafe assumptions, and missing tests.

If a required environment or hardware is unavailable, state exactly what was and was not verified. Do not fabricate evidence.

## Long-session expectations

- For a substantial task, continue through investigation, implementation, tests, failure diagnosis, fixes, and final review in the same session where feasible.
- Do not repeatedly ask for permission for ordinary engineering decisions. Ask at most one focused question when a genuine blocker requires user input.
- Do not abandon a task merely because the first build or CI run fails; use the failure as evidence and continue debugging.
- Do not create arbitrary freezes, handoffs, or stop conditions. Stop only when the task is complete, a real external blocker prevents progress, or further changes would be unsafe without user input.
- Keep progress narration minimal. Spend the session on engineering work.
- If scope is too large for one session, finish a useful, integrated milestone and leave precise next steps grounded in the actual code.

## VantaOS-specific constraints

- VantaOS is a from-scratch x86-64 C/NASM operating system. Respect the existing boot handoff, architecture layer, kernel subsystem boundaries, and userspace separation.
- Treat the repository's architecture checker, syscall ABI checker, host tests, QEMU regressions, and CI diagnostic markers as important contracts.
- Preserve safe UEFI/live-boot storage protections and never weaken destructive-operation safeguards as a shortcut.
- Keep hardware claims conservative: QEMU success is not physical-hardware validation.
- Do not add launcher-specific assumptions to the kernel or make the OS architecture depend on Vanta Launcher.
- Follow existing project documentation for build and test commands; inspect scripts before assuming command behavior.

## Completion report

At the end, report concisely:
- Root cause or capability delivered.
- Important files and architectural changes.
- Tests/builds actually run and their outcomes.
- What remains unverified or blocked.
- The resulting commit/branch when changes were pushed.

Never say "done", "working", "tested", or "supported" beyond the evidence obtained.
