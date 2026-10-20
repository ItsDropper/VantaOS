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

## Current Intel and AMD status

Intel and AMD backend modules expose a match function, descriptor reporting, and the common lifecycle operation table. They deliberately report acceleration as unavailable and their initialization callbacks fail closed. This is a framework starting point, **not** a functioning Intel or AMD hardware driver.

Next Intel stages: device-specific generation tables; PCI BAR/resource ownership; GGTT and memory-region management; GuC firmware authentication/load and submission; engine reset and interrupt setup; fence/timeline handling; display-plane integration. Older Gen9 and Xe-class devices require distinct backend implementations rather than one generic register path.

Next AMD stages: ASIC-specific IP block discovery; PSP firmware and trust establishment; GMC/GART/VMID management; ring-buffer and doorbell setup; SDMA and graphics queue bring-up; interrupt/fence handling; display-engine integration. GCN and RDNA generations need separate hardware tables and initialization paths.

## NVX status and sequencing

NVX remains the most advanced backend in this repository. Its host-tested GA10x primitives include identity/fuse inspection, WPR planning, runtime ELF materialization, DMA artifacts, Falcon reset/DMA/BROM/BOOTVEC/STARTCPU helpers, and queue/RPC framing. They are not yet connected into a complete SEC2/FWSEC-authenticated GSP boot sequence. No physical RTX 3060 boot or hardware acceleration is verified.

The next NVX milestones are to parse the exact SEC2/FWSEC firmware descriptors, wire the documented secure boot order using actual validated artifacts, establish the real VRAM/WPR mapping and metadata, wait for the GSP RISC-V runtime, and complete the first GSP-RM RPC. Do not publish acceleration capabilities until command submission and synchronization have succeeded on real hardware.

## Verification

`scripts/test_gpu_vendor_framework.c` is host-only and compiled with `-std=c11 -Wall -Wextra -Werror`. It checks Intel/AMD/NVIDIA classification, unsupported IDs, auxiliary-function rejection, forged descriptor rejection, capacity-safe inventory, lifecycle transitions, and capability guards. Passing these tests does not demonstrate a physical GPU driver.
