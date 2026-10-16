# ADR 0001: Subsystem-owned source manifests

- **Status:** Accepted
- **Date:** 2026-10-16

## Context

As the kernel grows, one central list of every C and assembly file becomes difficult to review and easy to leave stale. Source layout also needs to communicate ownership between architecture, memory, drivers, filesystem, and OS-facing components.

## Decision

Each major subsystem declares its source files in a nearby Makefile. The root Makefile includes these manifests and owns shared compiler flags, dependency generation, object placement, and final linking. `make list-sources` provides an inspectable view of the resulting source set.

This is Kbuild-inspired organization, not compatibility with Linux Kbuild.

## Consequences

**Benefits**
- Source membership is reviewed near the subsystem that owns it.
- New files have an explicit registration point.
- Common build policy remains centralized.
- Generated objects can mirror source paths without polluting source directories.

**Costs**
- Developers must update the correct manifest when adding a file.
- Include ordering and variable names become part of the build's internal contract.
- The manifests do not by themselves enforce clean dependency direction; review and architecture documentation remain necessary.

## Verification

The CI workflow runs `make list-sources`, checks for expected subsystem manifests, builds the kernel, and performs QEMU/UEFI regressions. This verifies the current registered build path, not every possible incremental-build scenario.
