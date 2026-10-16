# Architecture decision records

Decision records preserve the reasoning behind choices that are expensive to reverse or easy to misunderstand. They complement the current-state [architecture overview](../ARCHITECTURE.md); they do not replace it.

## When to write one

Create a record for decisions that change a subsystem boundary, boot or ABI contract, process or memory model, cross-CPU synchronization strategy, persistent on-disk format, or another long-lived compatibility constraint.

Do not use records as a task list or for routine bug fixes. Keep each record focused and link it from the relevant architecture documentation when useful.

## Suggested format

```markdown
# ADR-NNN: Short decision title

- Status: proposed | accepted | superseded | rejected
- Date: YYYY-MM-DD

## Context
What constraints and problem led to the decision?

## Decision
What is being chosen? Which invariants must remain true?

## Alternatives considered
What credible alternatives were evaluated, and why were they not chosen?

## Consequences
What becomes easier or harder? What risks, migration steps, or follow-up work remain?
```

Number records sequentially. Do not backdate a record or mark a proposal accepted before the project has actually chosen it.

No records have been added yet; add the first when a substantive architectural decision needs a durable explanation.
