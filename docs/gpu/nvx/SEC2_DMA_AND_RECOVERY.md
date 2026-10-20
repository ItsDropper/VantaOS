# NVX GA10x SEC2 DMA and recovery contract

**Target profile:** GA10x / GA106, SEC2 high-secure Booter Load path  
**Status:** implementation notes and testable software contract; physical validation pending

This note records the parts of the SEC2 DMA path that can be grounded in public NVIDIA code/register definitions and the limits of NVX's current implementation. It is deliberately not a claim that the RTX 3060 has booted NVX firmware.

## Evidence rules

- **Published source:** explicit behavior visible in NVIDIA's MIT-licensed Open GPU Kernel Modules or public register definitions.
- **NVX behavior:** what the current code does and what its synthetic tests prove.
- **Open question:** behavior that still needs exact GA106 register definitions, a matching firmware build, or physical measurements.

Primary source anchors:
- [NVIDIA GA102 GSP HAL](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/src/nvidia/src/kernel/gpu/gsp/arch/ampere/kernel_gsp_ga102.c)
- [NVIDIA SEC2/GSP booter handling](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/src/nvidia/src/kernel/gpu/gsp/kernel_gsp_booter.c)
- [NVIDIA TU102+ FWSEC/FRTS implementation](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/src/nvidia/src/kernel/gpu/gsp/arch/turing/kernel_gsp_frts_tu102.c)

The references are used to understand public contracts; NVX code is independently implemented. This document does not copy code from Nouveau.

## 1. The three address domains must remain separate

A Booter Load transfer involves three distinct address spaces:

1. **Host CPU virtual address** — used to parse/stage firmware and access the allocation from the OS.
2. **DMA-visible physical/GPU address** — programmed into Falcon DMA source registers; must be valid for the GPU's DMA path and the platform's mapping/ownership model.
3. **Falcon-local IMEM/DMEM offset** — the destination offset inside the SEC2 engine. It is not a host pointer and not the DMA source address.

NVX represents these separately in `nvx_dma_buffer_t` and the pure `nvx_gsp_secure_transfer_plan_t`. The plan currently uploads application code to SEC2 IMEM offset zero and OS data to DMEM offset zero, using source offsets derived from the parsed booter descriptor. The descriptor's boot vector and signature location are separate values: the boot vector is an image/IMEM-relative firmware entry, while the BROM signature parameter is DMEM-relative.

**Important unresolved platform contract:** a numeric `gpu_address` is not sufficient proof of a valid DMA mapping. A real backend must establish how physical pages are allocated, mapped, pinned, made coherent, and kept alive for the device. Synthetic allocations in host tests do not prove this.

## 2. DMA source address encoding and transfer granularity

NVX's GA10x helper validates the source address as nonzero, 256-byte aligned, and below the supported 49-bit address limit. It encodes the byte address into the DMA base registers as:
- low portion: `source_phys >> 8`;
- high portion: bits 40 through 48 of the byte address, shifted into the high register's low nine bits.

The helper also checks overflow for `source_phys + source_offset + length`, and rejects a transfer that crosses the implementation's 49-bit limit. The destination offset and source offset are required to be 256-byte aligned; the transfer length must be a multiple of 256 bytes. The current helper emits one command per 256-byte chunk and advances both source and destination offsets for each chunk.

These are **NVX's implemented validation rules**, grounded in the register field model and current supported profile. They are not a claim that every NVIDIA GPU generation or every Falcon engine uses identical limits. Keep the engine/profile explicit if extending this helper.

## 3. DMA command lifecycle

At a high level, NVX's helper follows this lifecycle:

1. Validate the BAR window, engine base, timeout parameters, address range, alignment, length, target memory kind, and DM-tag restrictions.
2. Poll the transfer-command register until the engine reports that the DMA command slot is not full.
3. Read the Falcon configuration/control registers and fail closed if the privileged window is locked or the Falcon is not halted.
4. Configure the FBIF physical-access/coherency policy and the DMA source base.
5. For each 256-byte chunk, wait for the command slot, program destination and source offsets, then issue the transfer command.
6. Poll until the command register reports idle; return a finite-timeout error if it never does.

The command path is not yet a full DMA subsystem. It does not establish IOMMU mappings, allocate GPU-visible pages, validate every physical-page mapping, or prove cache-coherency semantics for all VantaOS platforms. Those are responsibilities of the future memory backend. Do not infer device-side completion from a successful MMIO write alone; the helper's final poll is a separate completion condition.

## Signature selection: firmware baseline versus hardware fuse

The booter descriptor's `FUSE` field is not interchangeable with the live
hardware fuse version. The public Linux Nova GA102+ loader reads both: hardware
fuse version zero selects the final packed signature; otherwise it selects the
signature slot at `firmware_fuse_baseline - hardware_fuse_version`. The result
must be in the packed signature array. NVX now applies this relative selection
and rejects a hardware version newer than the firmware baseline or a computed
slot outside the available signatures before touching staging memory. This
replaces the earlier assumption that the hardware fuse value could always be
used directly as `signature_count - 1 - fuse_version`.

The synthetic preflight fixture now uses distinct load/unload firmware
baselines and verifies that changing the descriptor baseline changes the
selected signature. This is parser/selection evidence only: cryptographic
verification and real GA106 fuse behavior still require the actual firmware
provider and hardware.

## 4. Ownership is a safety boundary, not a bookkeeping detail

NVX models CPU/device ownership with `nvx_dma_sync_t`. Before SEC2 consumes the staged booter or WPR metadata, the executor transfers ownership to the device. The CPU must not mutate or free that memory while device access may be outstanding. After the transfer/boot helper returns, NVX synchronizes ownership back to the CPU before reading results or releasing the allocation.

The recovery invariant is:

> If a Falcon transaction may still be active, do not return device-owned memory to the allocator until a reset path has reported quiescence and the ownership transition succeeds.

The current `reset_and_reclaim()` first resets SEC2 and waits for the reset helper's completion condition. It then returns only the requested device-owned buffers to CPU ownership. If reset fails, reclamation fails closed. If ownership reclamation fails, the caller must not assume that the buffer is safe to free.

This invariant matters more than making every error path return quickly. A memory leak during a failed early bring-up is safer than freeing a page that active firmware may still DMA from.

## 5. What the phase trace now measures

The traced Booter Load entry point exposes:
- `current_phase`: operation currently attempted;
- `last_completed_phase`: last helper/ownership operation that returned success;
- `failed_phase`: phase associated with an error, or `NONE` on success;
- `result`: final returned NVX status;
- `recovery_trigger_status` and `recovery_result`: the original error that triggered recovery and the recovery outcome, kept separate so a reset failure does not erase the initiating DMA timeout;
- `recovery_attempted` / `recovery_succeeded`: reset/reclaim outcome for paths that invoke recovery;
- `sec2_quiescence_confirmed`: whether the reset/boot helper returned its success condition.

Synthetic fault injection now covers three distinct outcomes:
- an IMEM DMA timeout, where recovery succeeds and the last completed phase remains `TRANSFER_BOOTER_TO_DEVICE`;
- a DMEM DMA timeout after the IMEM chunks complete, where the trace identifies `UPLOAD_DMEM`, keeps `UPLOAD_IMEM` as the last completed phase, and records successful recovery;
- a DMA timeout followed by a reset that remains in the memory-scrubbing state, where recovery is reported as failed, quiescence is not claimed, and the booter buffer remains device-owned.

The third case only restores the fake BAR and ownership state after assertions so the test process can clean up. That test-only cleanup must not be interpreted as the driver successfully recovering from a stalled reset.

The test does **not** simulate a real SEC2, BROM, firmware signature check, or physical DMA engine. Its result demonstrates the error-handling code path under the fake BAR model only. The trace is software evidence, not cryptographic or hardware attestation.

## 6. Gaps to close before calling this a working NVIDIA driver

1. **Real BAR mapping:** map the correct GA106 BAR0 through the VantaOS PCI/MMIO layer and prove the register window size and access ordering.
2. **DMA allocator contract:** implement real device-visible allocations, address translation, lifetime/pinning, and architecture-appropriate cache/IOMMU handling.
3. **Register profile:** verify every used register offset and bit against the exact GA106 public register definitions; do not use a GSP base for SEC2 or silently generalize a GA102 helper.
4. **Real timeout/recovery:** record hardware register snapshots before and after timeout/reset; determine which status fields are safe to read when a privileged window is locked.
5. **Firmware identity:** bind descriptor parsing, signature selection, and command ABI to the exact VBIOS/GSP firmware profile.
6. **Secure boot result:** distinguish Falcon halt from verified FWSEC/BROM success and validate all documented scratch/mailbox postconditions.
7. **Runtime GSP:** only after secure setup, implement the matching GSP-RM initialization ABI and prove a real RPC round-trip.
8. **Acceleration:** only after firmware/RM startup, implement GPU VM, channels, command submission, interrupts/fences, VRAM management, and a rendering API.

## 7. Falsifiable next experiments

| Experiment | Pass condition | What it still would not prove |
| --- | --- | --- |
| Fault-injected IMEM DMA timeout | Trace identifies `UPLOAD_IMEM`; recovery outcome matches fake reset; ownership returns to CPU only after recovery | Real DMA behavior |
| Fault-injected DMEM DMA timeout | Trace identifies `UPLOAD_DMEM`; recovery result and ownership are asserted | Firmware acceptance |
| Fault-injected SEC2 reset timeout | Recovery reports failure, does not claim quiescence, and leaves the buffer device-owned | Physical reset behavior until run on hardware |
| Real read-only GA106 snapshot | BAR mapping, PCI identity, Falcon config/control/mailboxes captured without writes | Successful firmware execution |
| Real SEC2 boot attempt | Hardware-derived trace plus raw registers, firmware identity, and documented success postconditions | Full GSP-RM or graphics support |
| First GSP-RM RPC | Firmware responds through the matching ABI with validated result | Full GPU acceleration |

A row is complete only when its stated evidence exists. Build success and synthetic test success must not be promoted to a hardware pass.
