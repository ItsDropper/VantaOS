# GPU driver documentation

This directory documents the vendor-neutral GPU framework and vendor-specific bring-up. Claims are split by implementation, host-test evidence, and physical-hardware validation.

## NVX — NVIDIA GA10x

- [GA10x GSP bring-up and implementation status](nvx/GA10X_BRINGUP.md) — current hardware-facing primitives, staged boot path, and blockers.
- [GA10x firmware reverse-engineering notebook](nvx/FIRMWARE_REVERSE_ENGINEERING.md) — field layouts, address domains, signature-selection rules, source evidence, and next research tasks.
- [SEC2 transaction trace](nvx/SEC2_TRANSACTION_TRACE.md) — phase-level execution evidence and recovery diagnostics.
- [SEC2 DMA and recovery contract](nvx/SEC2_DMA_AND_RECOVERY.md) — address domains, DMA lifecycle, ownership invariants, fault injection, and hardware-validation gates.
- [VBIOS FWSEC metadata research](nvx/VBIOS_FWSEC_RESEARCH.md) — parser-specific constraints and VBIOS pointer chain.
- [GPU framework and bring-up plan](FRAMEWORK.md) — generic GPU lifecycle and Intel/AMD/NVX maturity.

## Vendor-neutral framework

The generic PCI classification and lifecycle framework is implemented separately from vendor firmware and execution engines. Intel and AMD backends remain lifecycle/matching scaffolds and do not advertise hardware acceleration. See [GPU framework status](FRAMEWORK.md) in the main documentation index.
