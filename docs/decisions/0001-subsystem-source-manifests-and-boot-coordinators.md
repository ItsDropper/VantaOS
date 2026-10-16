# ADR-001: Subsystem-owned source manifests and boot coordinators

- Status: accepted
- Date: 2026-10-16

## Context

The kernel and OS initialization paths are integration points: they establish ordering and connect subsystems. Over time, regression fixtures and subsystem-specific assertions can accumulate in those coordinators. That creates unrelated dependencies, makes boot order harder to audit, and encourages one file to become the place where every subsystem is tested.

VantaOS already uses subsystem Makefiles to make linked source ownership explicit. The same ownership principle should apply to test code and initialization responsibilities.

## Decision

1. Each subsystem owns a source manifest for its implementation and its test-only translation units.
2. Boot coordinators establish initialization order and call narrow subsystem entry points; they do not implement subsystem regression fixtures.
3. Test-only implementations are gated by the explicit self-test build flag and expose a small interface to the coordinator.
4. Architecture-sensitive behavior remains tested through the existing QEMU and UEFI/OVMF paths. Moving a test must preserve its assertion and diagnostic marker unless the test contract is deliberately changed.
5. A lightweight CI architecture check protects the most important source boundaries against accidental regression.

This is a structural rule, not a requirement to create an abstraction for every function or to split files solely by line count.

## Alternatives considered

- **Keep tests beside initialization code:** fewer files, but test fixtures and dependencies obscure boot ordering.
- **Move all tests into one global test file:** centralizes execution but creates a new monolith and weak subsystem ownership.
- **Create a general test framework:** unnecessary for the current freestanding kernel; a small set of explicit entry points is sufficient.
- **Rely on review conventions alone:** easy to introduce regressions silently; a small automated boundary check provides a repeatable guard.

## Consequences

- Source manifests and test modules become more explicit and easier to audit.
- A refactor must preserve test semantics and marker contracts, not just compile.
- CI may reject code that reintroduces test implementations into boot coordinators; intentional changes must update the rule and decision record in the same reviewed change.
- The boundary check is deliberately narrow. It does not prove dependency purity, race freedom, or correctness of the whole architecture.
