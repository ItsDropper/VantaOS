# NVX GSP bring-up: next concrete milestone

## Evidence-backed sequence

The public NVIDIA Open GPU Kernel Modules bootstrap sequence is not simply "load ELF and set START". For TU102+ GSP it resets the Falcon into RISC-V mode, programs the LIBOS boot-argument address through the architecture HAL, and then observes SEC2/GSP startup status before treating initialization as complete. See NVIDIA's implementation:
- https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/src/nvidia/src/kernel/gpu/gsp/arch/turing/kernel_gsp_tu102.c
- https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/src/nvidia/src/kernel/gpu/falcon/arch/ampere/kernel_falcon_ga102.c

## NVX status and hard boundary

NVX has bounded Falcon MMIO inspection, a RISC-V-active poll, BROM parameter programming, reset/start helpers, DMA upload helpers, and a modern cached-init-args/queue-layout builder. These are component-level primitives, not yet proof of an end-to-end GSP boot. In particular, setting a boot vector and seeing a control bit is insufficient: boot arguments must match the firmware ABI, firmware must be authenticated by hardware, and GSP must signal a documented ready state before RPCs can be trusted.

## Next implementation task

Wire a single GA10x bring-up transaction that records every phase and refuses to advance unless its prerequisite is evidenced:
1. Validate the exact firmware branch/ABI and required boot resources.
2. Verify DMA mappings and ownership for runtime image, bootloader, boot arguments, and shared queue allocation.
3. Stage firmware only through the supported SEC2/FWSEC sequence.
4. Issue the architecture-correct RISC-V reset/boot handoff.
5. Wait with a bounded deadline for documented hardware/firmware readiness, not merely ACTIVE.
6. Initialize command/status queues only after firmware readiness.
7. Send a harmless, known-valid initialization RPC and validate its response.
8. On any failure, preserve the original phase/error, stop queue use, and reclaim memory only after hardware quiescence is established.

Do not fake missing stages with success flags or speculative register writes. Use independent implementation research and register contracts; do not copy Nouveau source. Validate on QEMU only for software contracts, and require physical GA106 testing for authentication, RISC-V startup, RPC success, and acceleration claims.

## Evidence required before claiming a usable driver

A real RTX 3060 bring-up log must show device identification, supported firmware selection, successful hardware authentication, GSP ready indication, successful RPC round trip, and a first hardware-backed operation with verified output. Unit tests and CI cannot establish these hardware facts.
