# ADR 0002: Normalize boot information at the kernel boundary

- **Status:** Accepted
- **Date:** 2026-10-16

## Context

The project has a GRUB/Multiboot entry path and a separate UEFI loader path. Letting memory management and other kernel services depend directly on loader-specific structures would couple core subsystems to the selected boot mechanism and make a second boot path harder to maintain.

## Decision

Boot paths translate their available firmware/loader data into a versioned VantaOS boot-information contract. The kernel validates the contract and consumes normalized fields instead of treating raw loader structures as the general kernel interface.

The contract is an internal project ABI and may evolve deliberately. It is not a promise of compatibility with external operating systems or bootloaders.

## Consequences

**Benefits**
- Loader-specific parsing stays near the loader/entry boundary.
- Core initialization can consume a consistent representation.
- Contract magic, version, size, ranges, and optional fields can be validated in one place.

**Costs**
- Each boot path must populate the contract correctly.
- Versioning and validation add code that must be tested.
- Legacy handoff paths may still exist during migration and must not be mistaken for the canonical interface.

## Verification and remaining work

CI checks selected boot-info adapter cases and allocator behavior, runs the GRUB-based QEMU matrix, and runs the UEFI/OVMF kernel-handoff regression. These checks are not exhaustive malformed-input fuzzing or proof of compatibility with arbitrary firmware.
