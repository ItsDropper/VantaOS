# Engineering model

This document defines how VantaOS describes its own maturity and how subsystem changes should be designed and reviewed. It is intentionally separate from the implementation map in [Architecture](ARCHITECTURE.md).

## 1. Evidence levels

Use claims that match available evidence:

| Level | Meaning | Example of an acceptable claim |
| --- | --- | --- |
| Designed | A documented proposal or interface exists. | "The bootloader plan describes a future handoff." |
| Implemented | Code paths exist in the repository. | "The kernel contains a FAT32 directory reader." |
| Exercised | A test invokes the path. | "The QEMU regression exercises the directory-listing path." |
| Verified | A named test passed on a specified configuration. | "The 4-vCPU QEMU scheduler regression passed in CI." |
| Supported | The project has repeatable validation and defined expectations for that environment. | State the exact emulator or hardware configuration and known limitations. |

A code symbol, successful compile, or one successful boot does not by itself establish end-to-end support. Documentation should state the test scope and avoid extrapolating beyond it.

## 2. Subsystem contracts

Before changing a subsystem, record the assumptions that neighboring subsystems rely on. Depending on the area, these include:

- **Boot:** calling convention, boot-information version, physical-memory reservations, stack, and paging state at handoff.
- **Interrupts and SMP:** interrupt-enable state, per-CPU data ownership, interrupt routing, lock ordering, and which contexts may call an operation.
- **Processes and scheduling:** process-state transitions, queue/table ownership, CPU affinity, context lifetime, and wakeup/reschedule behavior.
- **Memory management:** allocator ownership, mapping lifetime, alignment, reserved regions, and failure behavior when memory is exhausted.
- **VFS and filesystems:** path normalization, ownership of handles, mount lifetime, buffer bounds, and explicit unsupported operations.
- **Drivers:** hardware assumptions, timeout behavior, resource ownership, and how failure is reported to callers.
- **Desktop and terminal:** separation between presentation and kernel services, bounded input, and behavior when a service is unavailable.

If a contract is unknown, investigate and document it rather than inventing one. This table is a review guide, not proof that all contracts are already enforced.

## 3. Concurrency review

For shared state, answer these questions in code review:

1. Which CPU or execution context owns the state?
2. What prevents concurrent mutation?
3. What memory-ordering guarantee is required between producer and consumer?
4. Can the operation run from an interrupt handler, while interrupts are disabled, or while holding another lock?
5. What happens if a process exits, a CPU goes offline, or initialization fails during the operation?
6. Can the test distinguish genuine cross-CPU behavior from sequential execution or timer preemption?

Tests should assert externally observable invariants where possible. Diagnostic counters are useful only when their update and interpretation are race-safe.

## 4. Test layers

- **Build checks:** compiler, assembler, linker, source-manifest, and artifact validation.
- **Kernel self-tests:** bounded deterministic checks that do not require a full interactive environment.
- **QEMU regressions:** boot markers and subsystem assertions across the memory and virtual-CPU configurations declared by CI.
- **UEFI/OVMF regressions:** firmware boot, loader handoff, and kernel startup where covered by the workflow.
- **Manual/hardware checks:** visual output, physical devices, timing-sensitive behavior, and compatibility not reproduced by the automated environment.

Passing one layer does not imply that later layers passed. Pull-request summaries should report the actual layer and configuration tested.

## 5. Architectural change records

For a change that affects subsystem boundaries, boot contracts, process model, memory ownership, or long-term compatibility, add a short decision record under `docs/decisions/`. Include:

- **Status:** proposed, accepted, superseded, or rejected.
- **Context:** constraints and the problem being solved.
- **Decision:** the selected design and its invariants.
- **Alternatives:** credible options considered and why they were not selected.
- **Consequences:** costs, migration work, failure modes, and follow-up requirements.

Do not create a decision record for every small fix. Use them for decisions future contributors would otherwise have to rediscover from commit history.

## 6. Scope discipline

Prefer small, reviewable increments over speculative frameworks. Do not add abstraction layers without a concrete caller or invariant they protect. Avoid claiming that future architecture is implemented merely because an interface or roadmap exists.
