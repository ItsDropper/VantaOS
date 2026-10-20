# NVX source organization and build ownership

The NVX implementation was reorganized into subsystem folders under `drivers/gpu/nvx/`. This makes ownership visible in the tree without changing the public headers in `include/` or renaming public functions.

## Directory map

| Directory | Primary responsibility | Representative code |
|---|---|---|
| `core/` | NVIDIA PCI identity, inventory, resources, lifecycle and attach | `nvx_device.c`, `nvx_pci.c`, `nvx_attach.c` |
| `firmware/` | Firmware acquisition, parsing, preflight and secure startup orchestration | `nvx_gsp_secure_boot.c`, `nvx_gsp_vbios.c`, `nvx_gsp_fwsec_result.c` |
| `gsp/` | Falcon and GSP runtime protocol, queue/RPC transport, ELF loading and init/boot memory | `nvx_gsp_falcon.c`, `nvx_gsp_msgq_rpc.c`, `nvx_gsp_elf_load.c` |
| `memory/` | DMA, GPU VA, GPU page tables and memory objects | `nvx_dma_sync.c`, `nvx_gpuvm.c`, `nvx_object_memory.c` |
| `execution/` | Submission validation and software fence timelines | `nvx_submit.c`, `nvx_sync.c` |
| `interrupts/` | MSI/MSI-X discovery and route setup/rollback | `nvx_irq_caps.c`, `nvx_irq_route.c` |

## Build integration

`drivers/gpu/nvx/Makefile` is the single source list for the production build. It enumerates every NVX implementation under its new path, including the bounded PCI ROM parser. `.github/workflows/nvx-tests.yml` compiles individual source units directly for host tests; those paths were updated alongside the move. The workflow path filter covers `drivers/gpu/nvx/**`, so future subsystem files still trigger NVX CI.

Public headers and tests remain in their established locations to avoid gratuitous include/API churn. A source move should not require consumers to include implementation files or alter public ABI declarations.

## Review checklist for future files

1. Place new code in the directory that owns its primary responsibility.
2. Add it to `VANTAOS_NVX_C` in the NVX Makefile.
3. Update direct-source test compile commands when they reference the moved implementation.
4. Add strict-warning tests for parsers, encoders, state machines, and rollback paths.
5. Document evidence level: synthetic software test, public-source cross-check, or actual hardware observation.
6. Never treat a successful host test as proof of BROM authentication, FWSEC/WPR success, GSP runtime readiness, or hardware acceleration.

The refactor is intended to improve maintainability only; it does not imply that the hardware bring-up blockers have been solved.
