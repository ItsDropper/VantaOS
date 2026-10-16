# Contributing to VantaOS

VantaOS is an experimental operating-system project. Contributions should improve correctness, reproducibility, and clarity without overstating what has been verified.

## Before making a change

1. Read the [system design](docs/SYSTEM_DESIGN.md), [architecture overview](docs/ARCHITECTURE.md), and [testing guide](docs/TESTING.md) relevant to your work.
2. Check the current code and open issues/PRs before starting overlapping work.
3. Create a focused branch from `master`. Do not develop directly on the default branch.
4. Keep a recoverable copy of important work. Use QEMU and disposable images; never test against a host system disk containing important data.

## Change scope

- Prefer one coherent concern per pull request.
- Avoid unrelated formatting churn and broad rewrites mixed with functional changes.
- Keep architecture-specific code in the architecture layer and general policy in the appropriate kernel subsystem.
- Register new source files in the correct subsystem Makefile; do not rely on implicit source discovery.
- Update documentation when behavior, subsystem ownership, build steps, supported configurations, or test coverage changes.
- For decisions with long-term architectural consequences, add an ADR under `docs/adr/`.
- Do not introduce dependencies, generated artifacts, or claimed capabilities without explaining why they are needed.

## Validation expectations

Run the narrowest relevant checks first, then the broader checks where possible:

```sh
make list-sources
make
VANTAOS_SELF_TEST=1 ./build.sh
```

For boot, scheduler, memory, filesystem, or interrupt changes, inspect serial output and run the relevant QEMU regressions. For boot-path changes, include the UEFI/OVMF test as applicable. The exact CI matrix is documented in [Testing and verification](docs/TESTING.md).

In the pull request, report each validation as one of:
- **PASS** — command/configuration completed and assertions passed;
- **FAIL** — command/configuration ran and failed;
- **NOT RUN** — not executed, unavailable, or not applicable (explain why).

A successful compile is not evidence that the kernel boots. A QEMU boot is not evidence of physical hardware compatibility. Do not report tests you did not personally run or cannot verify from CI.

## Pull request checklist

- [ ] The change has a clear, bounded purpose.
- [ ] Relevant tests were run, or unrun tests are explicitly marked.
- [ ] New success markers follow real assertions and have stable names.
- [ ] Failure paths and unsupported cases are handled explicitly.
- [ ] Documentation and source manifests are updated where needed.
- [ ] No secrets, personal data, generated build outputs, or unrelated files are included.
- [ ] The description states limitations and evidence rather than broad claims.

## Review principles

Reviewers should focus on correctness, invariants, regression risk, and evidence before style preferences. Architecture changes should explain ownership, synchronization, failure behavior, and alternatives considered. A change that compiles but weakens a regression assertion is not automatically an improvement.

## Reporting security issues

Do not publish sensitive exploit details in a public issue. Follow [SECURITY.md](SECURITY.md).
