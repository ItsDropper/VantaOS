# GPU driver documentation

Status: framework implemented and host-tested; vendor backends are at different
maturity and **none is hardware-verified**. Last reviewed: 2026-11-13.

VantaOS has a vendor-neutral GPU framework (PCI classification, guarded
lifecycle, capability publication) and independent NVIDIA (NVX), Intel and AMD
backends. Intel and AMD are lifecycle/matching scaffolds with opt-in minimal
execution paths; NVX is the deep effort.

## Reading order by goal

| Goal | Read |
| --- | --- |
| Understand the GPU subsystem | [Framework](FRAMEWORK.md) |
| Understand NVX at a glance | [NVX overview](nvx/OVERVIEW.md) |
| Run NVX diagnostics on a real card | [Passthrough testing](nvx/PASSTHROUGH_TESTING.md), then [commands](nvx/BRINGUP.md) |
| Know how hardware facts are established | [Independence policy and knowledge base](nvx/INDEPENDENCE.md) |
| See what blocks "game ready" | [RTX 3060 readiness gates](nvx/RTX3060_READINESS.md), [GSP bootstrap gaps](nvx/GSP_BOOTSTRAP_GAPS.md) |
| Work on the code | [Source layout](nvx/SOURCE_LAYOUT.md), [GA10x bring-up](nvx/GA10X_BRINGUP.md), [Platform coordinator](nvx/PLATFORM_COORDINATOR.md) |

## NVX reference documents

| Document | Contents |
| --- | --- |
| [GA10x bring-up](nvx/GA10X_BRINGUP.md) | Implemented primitives and staged boot path |
| [Platform coordinator](nvx/PLATFORM_COORDINATOR.md) | Orchestration of VBIOS, firmware, WPR, DMA and startup |
| [GSP bootstrap gaps](nvx/GSP_BOOTSTRAP_GAPS.md) | Remaining integration blockers |
| [Firmware reverse-engineering notebook](nvx/FIRMWARE_REVERSE_ENGINEERING.md) | Field layouts, address domains, signature selection |
| [VBIOS FWSEC research](nvx/VBIOS_FWSEC_RESEARCH.md) · [ROM chain research](nvx/VBIOS_ROM_CHAIN_RESEARCH.md) | Parser constraints |
| [SEC2 DMA and recovery](nvx/SEC2_DMA_AND_RECOVERY.md) · [SEC2 transaction trace](nvx/SEC2_TRANSACTION_TRACE.md) | Ownership invariants, diagnostics |
| [NVX architecture (design record)](../NVX_ARCHITECTURE.md) | Long-form design |

## Verification boundary

A PCI identity, an assigned BAR, a mapped MMIO window or a synthetic register
test does not prove a GPU is initialized. No Intel, AMD or NVIDIA acceleration
is claimed.
