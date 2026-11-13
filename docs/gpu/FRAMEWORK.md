# GPU driver framework and bring-up plan

## Architecture

- `include/gpu.h`: common device states, identity and capability bits.
- `include/gpu_vendor.h` / `drivers/gpu/core/gpu_vendor.c`: read-only PCI display-function classification and curated device-ID architecture hints.
- `include/gpu_core.h` / `drivers/gpu/core/gpu_core.c`: common device object, guarded lifecycle transitions, capability publication, driver matching and bounded PCI inventory.
- `drivers/gpu/nvx/`: NVIDIA-specific GA10x identity, BAR0/Falcon, firmware container, DMA, GSP and GPUVM work.
- `drivers/gpu/intel/`: Intel backend boundary.
- `drivers/gpu/amd/`: AMD backend boundary.
- `drivers/gpu/display/`: vendor-independent display/presentation work.

The generic inventory is read-only. It does not map MMIO, alter PCI command bits, allocate DMA, reset devices, or enable bus mastering. A recognized vendor does not imply a recognized GPU model; unknown IDs remain architecture-unknown rather than being guessed from broad ID ranges.

## Intel and AMD common-layer behavior

Intel and AMD backend modules expose a match function, descriptor reporting, and the common lifecycle operation table. The common driver initialization now enumerates PCI display functions and reports vendor, device ID, known architecture, and whether an assigned/enabled MMIO aperture is present. This path is read-only: it does not reset hardware, change PCI decode, map guessed register windows, load firmware, enable bus mastering, or advertise acceleration. Initialization callbacks still fail closed. This is a better-tested bring-up and diagnostics stage, **not** a functioning accelerated Intel or AMD hardware driver.

Next Intel stages: device-specific generation tables; PCI BAR/resource ownership; GGTT and memory-region management; GuC firmware authentication/load and submission; engine reset and interrupt setup; fence/timeline handling; display-plane integration. Older Gen9 and Xe-class devices require distinct backend implementations rather than one generic register path.

Next AMD stages: ASIC-specific IP block discovery; PSP firmware and trust establishment; GMC/GART/VMID management; ring-buffer and doorbell setup; SDMA and graphics queue bring-up; interrupt/fence handling; display-engine integration. GCN and RDNA generations need separate hardware tables and initialization paths.

## Current status by vendor

### NVIDIA / NVX

Status: implemented and host-tested; not hardware-verified. See the
[NVX overview](nvx/OVERVIEW.md) for the boot chain and status matrix.

The GA10x backend includes PCI attach, bounded BAR/MMIO access, PMC identity,
VBIOS read through the PROM window, five-component GSP firmware acquisition
verified against a pinned SHA-256 manifest, structural preflight, WPR
layout/heap policy, SEC2/FWSEC/FRTS and Booter Load sequences, GSP RISC-V
launch and readiness, MSGQ linking, GMCAPI initialization and a GSP-RM
initialization RPC. The composition is callable as
`nvx_gsp_ga10x_start_and_init_rm_from_pci()` and orchestrated by
`nvx_gsp_platform_start()`, but **no boot-time or shell path invokes it**. The
shell commands `nvx status|survey|vbios|fw` are read-only diagnostics
([manual bring-up](nvx/BRINGUP.md)).

Open items: the WPR/FRTS/heap policy is a reference-informed hypothesis pending
experiments ([independence policy](nvx/INDEPENDENCE.md)); the GSP system-info
key set and init-argument layout for the matching firmware release are not
established; there is no physical GA106 validation. The PCI BAR sizing path
refuses to probe while decode or bus mastering is enabled, so a safe handoff
from a live firmware-driven GPU remains an integration issue.

Next milestone: run the diagnostics on a real card
([passthrough testing](nvx/PASSTHROUGH_TESTING.md)), settle the unverified
facts, then wire `nvx start` and validate a CPU-verified copy-engine operation.

### Intel

PCI family classification and read-only BAR discovery are implemented.
Hardware initialization remains deliberately disabled.

The first scoped profile is Skylake Gen9 GT2 (`8086:1912`, `8086:1916`). The GGTT memory runtime maps the audited 16 MiB GTTMMADR BAR0, allocates contiguous DMA, installs/verifies GGTT PTEs only into empty slots, preserves pre-existing firmware/scanout mappings, invalidates the GTT cache, rejects overlapping GPU VAs and reclaims addresses only after successful unmap/invalidation. The separate execlist runtime consumes CSB completion events and validates fence writeback, but still requires a caller-prepared LRCA/context and command ring. GuC firmware loading, LRCA/context-image construction, command-batch generation, integration of memory allocations into a self-created render context, reset/recovery and physical-hardware validation remain unfinished.

### AMD

PCI family classification and read-only BAR discovery are implemented.
Hardware initialization remains deliberately disabled.

The first scoped profile is Polaris 10/20 (`1002:67DF`, `1002:67C0`). The device runtime now sequences BAR5 mapping, native DMA fault-page allocation, VMID-1 root installation, GPU-VA allocation, VMID-1 GFX-ring setup and a fenced PM4 submission. It exposes allocation/free APIs for additional GPU-visible buffers and pins backing DMA until unmap plus TLB invalidation succeed. PSP/GFX firmware authentication/loading is still external and must complete before this path can start. SDMA setup, interrupts, reset/recovery, display integration and physical hardware validation remain unfinished. Do not treat all GCN/RDNA families as register-compatible.

## Verification boundary

A PCI identity, assigned BAR, successful MMIO mapping, or synthetic register
test does not prove that a GPU is initialized. No Intel or AMD 3D acceleration
is currently claimed, and NVX's secure boot sequence is not equivalent to a
working graphics command path. The next implementation should target one
specific GPU generation end-to-end instead of adding placeholder capability
flags for every family.

## Verification

`scripts/test_gpu_vendor_framework.c` is host-only and compiled with `-std=c11 -Wall -Wextra -Werror`. It checks Intel/AMD/NVIDIA classification, unsupported IDs, auxiliary-function rejection, forged descriptor rejection, capacity-safe inventory, lifecycle transitions, and capability guards. Passing these tests does not demonstrate a physical GPU driver.


Detailed NVX engineering records are grouped under [GPU documentation](README.md).