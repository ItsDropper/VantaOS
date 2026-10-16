# Contributing to VantaOS

VantaOS is an experimental x86-64 operating-system project. Contributions should improve a verifiable subsystem without implying that prototype behavior is production-ready.

## Before changing code

1. Read the [architecture overview](docs/ARCHITECTURE.md) and the document for the subsystem you intend to change.
2. Check the [development plan](docs/DEVELOPMENT_PLAN.md) and recent pull requests to avoid duplicating or contradicting active work.
3. Identify invariants, ownership rules, interrupt-state assumptions, and failure paths before editing kernel code.
4. Keep changes focused. Separate documentation, refactoring, and behavioral changes when that makes review safer.
5. For changes to subsystem boundaries, boot handoff, process/memory ownership, or cross-CPU synchronization strategy, check [architecture decision records](docs/decisions/README.md) and add or update a record when the decision will be costly to reverse.

## Development workflow

1. Start from an up-to-date `master` and create a topic branch.
2. Make the smallest coherent change that addresses the stated problem.
3. Build with `./build.sh`; run the relevant checks described in [Testing](docs/TESTING.md).
4. For boot, scheduler, memory, filesystem, or interrupt changes, run the relevant QEMU regressions. Changes to boot handoff or firmware paths must also pass the UEFI/OVMF regression.
5. Inspect the final diff for unrelated changes, generated artifacts, stale comments, and claims not backed by tests.
6. Open a pull request describing the problem, design, validation, limitations, and any follow-up work.

## Kernel engineering expectations

- Treat interrupt state, lock ordering, CPU affinity, memory ownership, and object lifetime as explicit contracts.
- Do not block, allocate, or invoke callbacks while holding a lock unless the subsystem's contract explicitly permits it.
- Make cross-CPU shared-state accesses use an appropriate atomic or synchronization primitive; `volatile` alone is not synchronization.
- Keep interrupt handlers bounded and avoid unsafe assumptions about which CPU or stack is active.
- Check integer bounds, buffer sizes, error returns, and partial initialization cleanup.
- Preserve serial diagnostics used by regression tests. Emit success markers only after the associated assertions pass.
- Do not describe kernel-mode tasks as isolated user processes, a namespace as persistent storage, or a detected device as fully supported hardware.

These are review principles, not a claim that every existing subsystem already satisfies them.

## Documentation changes

Update documentation when behavior, build commands, subsystem boundaries, support status, or test requirements change. Clearly distinguish:

- **Implemented** — code exists.
- **Tested** — a named check exercises the behavior.
- **Verified** — the stated test currently passes in a specified environment.
- **Planned** — intended future work, not available functionality.

Prefer precise limitations over broad claims. Keep links relative so documentation works on GitHub and in local clones.

## Pull request checklist

- [ ] Scope is focused and the diff contains no unrelated changes.
- [ ] Build and relevant regression tests were run, or the reason they could not be run is stated.
- [ ] New behavior has a test or a documented reason testing is not yet feasible.
- [ ] Failure paths and concurrency assumptions were considered.
- [ ] Documentation reflects actual behavior and limitations.
- [ ] No generated build outputs, private data, or unrelated binaries were added.
