# Architecture decision records

Architecture decision records (ADRs) capture decisions whose reasoning would otherwise be lost in commit history. They complement, rather than replace, the current-state [system design](../SYSTEM_DESIGN.md).

## When to write an ADR

Write one when a change affects a durable interface or has a meaningful trade-off, for example:
- boot protocol or boot-information ABI;
- subsystem ownership or source/build organization;
- scheduler, SMP synchronization, or memory model;
- filesystem/VFS contract;
- dependency policy or test strategy.

Small local implementation choices do not need an ADR.

## Status vocabulary

- **Proposed** — under discussion; not a project commitment.
- **Accepted** — decision is current.
- **Superseded** — replaced by a later ADR; link the replacement.
- **Retired** — no longer applicable.

An accepted ADR records intent, not proof of implementation or correctness.

## Format

Use a numbered file such as `0003-short-title.md` with:
1. Title and status
2. Context and constraints
3. Decision
4. Consequences and trade-offs
5. Verification/evidence and follow-up

Keep the list chronological. If a decision changes, add a new ADR instead of silently rewriting history.

## Records

- [ADR 0001: Subsystem-owned source manifests](0001-subsystem-source-manifests.md)
- [ADR 0002: Normalize boot information at the kernel boundary](0002-normalized-boot-info.md)
