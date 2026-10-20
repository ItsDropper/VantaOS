# NVX source layout

NVX is the NVIDIA-specific GPU driver subsystem. Public interfaces and shared ABI declarations remain in the repository-wide `include/` directory; implementation translation units are grouped here by responsibility.

| Directory | Ownership |
|---|---|
| `core/` | PCI identity/inventory, device lifecycle, BAR resource validation, MMIO, probe and attach transactions |
| `firmware/` | Firmware acquisition/bundles, image preflight, secure Booter Load orchestration, VBIOS/PCIR parsing, FWSEC commands/results and WPR metadata |
| `gsp/` | Falcon/GSP register logic, MSGQ transport, RPC framing/correlation, ELF loading, init arguments and boot-memory coordination |
| `memory/` | DMA allocation/synchronization, GPU virtual-address ranges, page tables and memory-object ownership |
| `execution/` | Software fence timelines and submission validation |
| `interrupts/` | MSI/MSI-X capability inspection and interrupt-route transactions |
| `Makefile` | Canonical NVX source list consumed by the top-level GPU build |

## Build and tests

The canonical production source list is `VANTAOS_NVX_C` in this directory's `Makefile`. Keep source additions there and add strict-warning host tests to `.github/workflows/nvx-tests.yml` when the logic can be tested without hardware. The workflow's `drivers/gpu/nvx/**` path filter intentionally includes every subsystem folder.

Headers have not been moved: tests, platform code, and other subsystems consume the existing `include/nvx_*.h` interfaces. Moving implementations therefore must not silently rename exported declarations or change ABI layouts.

## Dependency direction

- `core/` owns discovery and device attachment, not GSP firmware protocol details.
- `firmware/` may coordinate `gsp/` and `memory/` contracts for secure startup.
- `gsp/` uses explicit MMIO, DMA, and firmware inputs; it must not claim runtime readiness until observable protocol gates pass.
- `execution/` and `interrupts/` remain separate from firmware authentication and GSP startup.
- Cross-subsystem contracts belong in public headers under `include/`; avoid including a sibling implementation `.c` file.

This is an organizational refactor, not a claim of complete NVIDIA hardware support. All synthetic tests remain software evidence only.
