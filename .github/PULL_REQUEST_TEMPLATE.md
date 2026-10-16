## Summary

Describe the problem and the intended outcome. Link related issues or decision records.

## Design and scope

- **Subsystem(s):**
- **Behavior or contract changed:**
- **Alternatives considered:**
- **Known limitations / follow-up:**

## Validation

- [ ] `./build.sh` succeeds.
- [ ] Relevant self-tests pass.
- [ ] Relevant QEMU regressions pass, including the applicable memory and vCPU configurations.
- [ ] UEFI/OVMF regression passes when boot or handoff paths are affected.
- [ ] Documentation reflects the actual behavior and evidence level.
- [ ] Diff reviewed for generated files, unrelated changes, and accidental artifacts.

**Commands and results:**

<!-- List the exact commands and outcomes. If a check was not run, say why. -->

## Risk review

- Interrupt, lock-order, and cross-CPU effects:
- Memory ownership, bounds, and cleanup:
- Failure behavior and diagnostics:
- Compatibility or migration impact:
