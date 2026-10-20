# NVX GA10x GSP Falcon bring-up notes

This note records the next hardware boundary for Ampere GA10x (including the
RTX 3060 GA106 family). It separates register observations from claims about
firmware execution.

## Independently checked register facts

NVX's GA10x read-only inspector uses the following BAR0-relative locations:

| Register | BAR0 offset | Purpose |
| --- | ---: | --- |
| GSP Falcon mailbox 0 | `0x110040` | Firmware-to-host status mailbox |
| GSP Falcon mailbox 1 | `0x110044` | Secondary firmware mailbox |
| GSP Falcon HWCFG2 | `0x1100F4` | Reset-ready, memory-scrub, and RISC-V capability bits |
| GSP Falcon CPUCTL | `0x110100` | Falcon halted/start control state |
| GSP RISC-V CPUCTL | `0x111388` | RISC-V active/halted state |

The register locations and decoded bits were cross-checked against the public
Linux Nova register definitions:
https://github.com/torvalds/linux/blob/master/drivers/gpu/nova-core/regs.rs

The GSP Falcon windows are based at `0x110000` and `0x111000`, respectively:
https://github.com/torvalds/linux/blob/master/drivers/gpu/nova-core/falcon/gsp.rs

Linux Nova documents the `0xBADF41xx` read pattern as evidence that the GSP
PRIV target is still masked. NVX masks off the varying low byte and refuses to
initialize queue state when that pattern is observed. It does not attempt to
bypass the mask or write any register during this check:
https://github.com/torvalds/linux/blob/master/drivers/gpu/nova-core/falcon/gsp.rs

## Implemented in NVX

- `nvx_gsp_ga10x_inspect_falcon()` performs bounded, read-only MMIO reads and
  returns raw register values plus decoded reset-ready, memory-scrubbing,
  Falcon-halted/start-requested, RISC-V-present/active/halted, and mailbox
  suspended fields.
- It rejects truncated mappings and output objects that alias the BAR mapping.
- GA10x queue setup now requires the entire diagnostic register range to be
  mapped and refuses to mark the queue transport ready when the PRIV target is
  still masked.
- `nvx_gsp_ga10x_wait_riscv_active()` polls the actual RISC-V active bit with a
  caller-supplied scheduler delay, checks the PRIV lock on each iteration, and
  returns a distinct timeout rather than spinning forever.
- Host tests cover decoded bitfields, the locked-window signature, short BARs,
  null arguments, and alias rejection.

## Hardware-derived firmware fuse selection

NVX now reads the GA102+ FPF ucode-version arrays through the bounded BAR0
MMIO accessor. Register offsets are `0x824100` (NVDEC), `0x824140` (SEC2), and
`0x8241C0` (GSP); each array contains 16 32-bit slots, with the fuse bitmap in
bits 15:0. The returned version is the bitmap's bit length (highest set bit
plus one), matching the GA102 HAL/Nova logic; it is not the raw register value.
Engine masks must match one supported value exactly and ucode IDs must be in
the documented 1..16 range. Short mappings, unknown engine masks, invalid IDs,
and nonzero reserved bits fail closed.

`nvx_gsp_preflight_stage_booters_from_bar0()` connects this hardware read to the
actual booter staging path: it validates the bundle first, takes each booter's
engine mask and ucode ID from parsed firmware metadata, reads the matching
fuse version, then stages both payloads. The older explicit-version staging API
remains available for offline tests and tooling; it should not be used as the
physical-hardware path.

Register definitions and the bit-length conversion were cross-checked against
the [Linux Nova GA102 Falcon HAL](https://kernel.googlesource.com/pub/scm/linux/kernel/git/next/linux-next/+/2f03a3fe84216541ed29f9b7bc014acb5f1b676b/drivers/gpu/nova-core/falcon/hal/ga102.rs),
the [Nova fuse register definitions](https://lists.openwall.net/linux-kernel/2026/08/17/1877),
and [NVIDIA's GA102 GSP HAL](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/src/nvidia/src/kernel/gpu/gsp/arch/ampere/kernel_gsp_ga102.c).
The host tests exercise register bounds, engine/ucode rejection, bit-length
conversion, reserved-bit rejection, and no-staging-on-fuse-read-failure. These
tests use synthetic memory; they do not validate physical GPU fuse values.
## Booter payload staging

The public NVIDIA firmware extraction script documents the signed booter container
layout: a 24-byte outer header, a version-2 secure-load header, a packed signature
array, patch-location/signature metadata, patch metadata, signature count, load
descriptor, and firmware payload. The Linux NVIDIA open module selects the
signature with index `num_sigs - 1 - fuse_version` (when multiple signatures
exist) and rejects a nonzero `patch_sig` for this format.

The separate `bootloader-<version>.bin` is now parsed as the GA10x NVFW container too: version 1, a 24-byte outer header, an 84-byte descriptor, payload offset 108, and a 256-byte-aligned total size. Its payload can be copied into private staging, and the preflight report stores descriptor offsets rather than pointers that could outlive the verified bundle.\n\nNVX now implements that data path in `nvx_gsp_image_prepare()`: it copies only the
firmware payload into private staging, selects the fuse-indexed signature, and
patches it at the payload-relative location. The integrated
`nvx_gsp_preflight_stage_booters()` path validates the whole bundle, prechecks
both fuse versions/capacities/aliasing, and stages load plus unload images as a
single fail-closed operation. It rejects unsupported fuse
versions, truncated staging buffers, input/output aliasing, and signatures that
would extend outside the payload's DMEM range. Host tests cover both signature
orders and ensure failure leaves the staging buffer untouched.

Research references:
- https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/nouveau/extract-firmware-nouveau.py
- https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/src/nvidia/src/kernel/gpu/gsp/kernel_gsp_booter.c

The parser also enforces the actual v1 layout documented by NVIDIA's extraction script: signature array immediately after the secure-load header, patch metadata and count immediately after the signature array, exactly one application descriptor, and payload immediately after the descriptor. It rejects unsupported container versions, nonzero patch_sig, invalid app layout, and unaligned outer sizes.\n\nThis is signature **selection and placement**, not cryptographic authentication
and not a firmware launch. The hardware SEC2/FWSEC fuse-version read and actual
DMA upload/boot sequence are still missing.

## What this does not mean

A readable Falcon register window does not mean the GSP is running. NVX now has
low-level reset/scrub, 256-byte Falcon DMA upload, BROM-parameter, BOOTVEC/STARTCPU,
and mailbox-completion primitives for SEC2/GSP. They are independently bounded
and host-tested, but they are not yet wired into one secure-boot transaction.
NVX still does not authenticate NVIDIA's signed firmware cryptographically,
extract FWSEC from a physical VBIOS image or execute the prepared FWSEC-FRTS commands,
allocate/write the actual WPR in VRAM, validate firmware-specific mailbox status,
activate the GSP RISC-V runtime, or complete a genuine GSP-RM initialization RPC.
The queue and DMA tests use fake BAR memory; they cannot prove physical GPU support.

The FWSEC interface parser and pure SB/FRTS command-record builder are now implemented and host-tested. The next implementation step is to bind that profile to a verified firmware identity, reconstruct command-result/scratch-register semantics, then connect the records to a fail-closed SEC2 transaction with verified signing rules, DMA ownership, WPR metadata, and bounded hardware polling. Do not substitute guessed register writes or
a fake "boot succeeded" flag for these requirements. NVIDIA's open kernel
module and Linux Nova are research references. Their relevant files have MIT
or GPL licensing depending on the source; verify each file's license before
adapting any implementation rather than assuming all reference code is GPL.


## Runtime ELF materialization (host-verified, not hardware boot)

The new `nvx_gsp_elf_load_segments()` path reparses the acquired runtime ELF,
rejects ET_DYN until a relocation policy exists, resolves every PT_LOAD target
through a platform-supplied mapping callback, rejects overlapping physical and
CPU mappings, and validates every mapping before it writes anything. It copies
file-backed bytes and zeroes each segment's BSS tail. The callback contract
requires the caller to map only memory reserved and addressable by the GSP; the
loader does not allocate memory, alter PCI bus mastering, or start the engine.

`nvx_gsp_preflight_load_runtime()` joins this operation to full GA10x firmware
bundle preflight. This makes segment materialization callable only after the
bundle and its firmware assets have passed the existing preflight checks. Host
tests cover data copy, BSS clearing, destination alias rejection, unsupported
ET_DYN, and no-write behavior when mapping fails.

This still is **not a complete boot path**. No GA10x secure-boot/SEC2 FWSEC
transaction, firmware-authenticated WPR placement, VRAM-resident WPR allocation,
Falcon DMA transfer, reset/boot register sequence, or GSP-RM init RPC is wired
to physical hardware. The loader's map callback must not be mistaken for a
real BAR1/VRAM mapping implementation. Do not mark the device initialized or
enable acceleration based on these host tests.


## System-memory boot artifacts and WPR handoff

`nvx_gsp_boot_memory_acquire()` now performs full bundle preflight before allocating
three distinct physically contiguous DMA buffers for the RISC-V runtime ELF,
the extracted GA10x bootloader payload, and the runtime's `.fwsignature_ga10x`
section. It copies only validated source ranges, checks physical ranges for
overlap, and unwinds every allocation if any later allocation or copy fails.
`nvx_gsp_boot_memory_bind_wpr()` transfers the actual DMA physical addresses
and exact byte lengths into a caller-owned WPR layout while preserving its
framebuffer offsets.

This closes the CPU-side SYSRAM artifact allocation/binding step; it does not
supply the missing VRAM/WPR allocator or bootloader descriptor semantics. The
platform still must derive the code/data/manifest offsets from the validated
GA10x descriptor, plan non-overlapping framebuffer regions against real FB
size, build and validate the 256-byte WPR metadata, perform the vendor-defined
SEC2/FWSEC secure boot flow, launch the GSP, and complete the first real GSP-RM
RPC. The allocator never enables PCI bus mastering or writes a boot register.


## WPR layout planning

Added `nvx_gsp_wpr_layout_plan()`, which applies the GA10x/Turing-derived WPR placement arithmetic with explicit device/platform inputs: usable framebuffer size, VBIOS workspace offset, optional MMU-lock lower bound, WPR end margin, FRTS size, and non-WPR heap requirement. It aligns the WPR end to 128 KiB, the boot image to 4 KiB, the runtime ELF to 64 KiB, and heap/meta regions to 1 MiB; it validates the completed layout before publishing it. WPR heap sizing is delegated to a required platform policy callback because that sizing depends on VantaOS memory-manager policy and firmware requirements.

This avoids inventing VRAM offsets. It does **not** discover the VBIOS workspace, read the MMU lock, determine usable framebuffer size, implement the platform heap policy, parse the bootloader's monitor-code/data/manifest offsets, or write metadata into actual VRAM. Those inputs must come from real hardware/platform integration before secure boot is safe.


The GA10x bootloader parser also now extracts the 84-byte descriptor's monitor-code,
monitor-data and manifest ranges, and bounds-checks non-empty sections against the
extracted payload. Boot-memory ownership retains those offsets and binds them into
the WPR metadata plan instead of requiring callers to hard-code them.


## Falcon Boot ROM verification setup

NVX now has `nvx_gsp_ga10x_program_brom()` for the GA102+ Falcon BROM parameter registers. The BAR0 offsets are derived from NVIDIA's published GA102 register header and independently cross-checked against Linux Nova's GA102 Falcon HAL:

| Register | BAR0 offset | Value |
| --- | ---: | --- |
| BROM parameter address | `0x111210` | validated, 4-byte-aligned signature/PKC parameter offset |
| BROM engine mask | `0x11119C` | exact supported engine mask from validated firmware metadata |
| BROM ucode ID | `0x111198` | 1-based ID in the documented 1..16 range |
| BROM algorithm selector | `0x111180` | RSA-3K (`1`) |

The write order matches Nova's GA102 HAL: parameter address, engine mask, ucode ID, RSA-3K selector. NVX validates the complete register window before writing, checks the PRIV-lock pattern, and refuses to program a Falcon unless CPUCTL reports it halted. Strict-warning host tests assert the exact written values and reject invalid engine masks, ucode IDs, alignment, active-engine state, locked PRIV access, and truncated BAR mappings.

This is a **real register-programming primitive**, not a completed secure-boot transaction. It does not upload FWSEC or GSP firmware, authenticate the image, select/reset the correct SEC2 versus GSP Falcon, program WPR/VRAM, set the boot vector, trigger the secure boot ROM, or establish that the firmware is executing. The higher-level boot path must call it only with parameters extracted from the matching validated firmware image and after the platform has completed the preceding load steps. No physical GA10x device was available to validate these writes.


## Guarded Falcon launch primitive

`nvx_gsp_ga10x_start_falcon()` now performs the GA102 Falcon launch register sequence for an image that the caller has already loaded and authenticated: it validates a complete BOOTVEC/CPUCTL mapping, checks the PRIV-lock signature and halted state, writes BOOTVEC, then issues STARTCPU. Host tests verify register order/results and reject misaligned entry points, running Falcon state, locked access, and truncated mappings.

This primitive does **not** load or authenticate FWSEC, configure SEC2, select the RISC-V core, create the framebuffer WPR, or prove a boot succeeded. The engine-aware `nvx_ga10x_falcon_start()` and `nvx_ga10x_falcon_boot()` APIs now support both SEC2 and GSP register windows; the GSP-named start API remains a wrapper. The full secure firmware path is still not connected.


## SEC2 and GSP engine-specific boot register paths

The BROM and Falcon launch helpers now support both documented GA102 engine windows:
- GSP Falcon: `0x110000`, second Falcon window `0x111000`, GSP engine mask `0x0400`.
- SEC2 Falcon: `0x840000`, second Falcon window `0x841000`, SEC2 engine mask `0x0001`.

The engine-aware API rejects mismatched base pairs and engine masks instead of allowing a SEC2 descriptor to be programmed into GSP registers (or vice versa). Host tests exercise both sets of real BAR0 offsets. This enables the SEC2/FWSEC stage to use the correct register window, but it still does not implement SEC2 firmware parsing/loading, the FWSEC handshake, WPR writes, or a verified GSP boot.


## Usable framebuffer size input

Added `nvx_gsp_ga10x_read_usable_fb_size()`, which reads BAR0 scratch register `0x1183A4` (NV_PGC6_AON_SECURE_SCRATCH_GROUP_42) and converts the documented MiB value into bytes. It does not use PCI BAR aperture size as a proxy for installed/usable VRAM. Zero/unavailable values and short BAR mappings fail closed. Strict host tests cover conversion, zero rejection, and mapping bounds.

Register definition was cross-checked against [Linux Nova's GA102 register definitions](https://github.com/torvalds/linux/blob/master/drivers/gpu/nova-core/regs.rs). This supplies one real input to WPR planning; VBIOS workspace, MMU-lock lower bound, FRTS sizing, heap policy, and framebuffer allocation/writes remain platform blockers.


## Falcon DMA firmware upload primitive

Added `nvx_ga10x_falcon_dma_upload()` for GA102 SEC2/GSP Falcon engines. It validates physical-address width and 256-byte alignment, validates IMEM/DMEM destination bounds, requires the engine halted and the PRIV window accessible, configures physical coherent SYSRAM FBIF access, programs the split DMA base registers, submits bounded 256-byte transfers, and polls queue-full/engine-idle status with explicit timeouts. Secure IMEM selects the secure transfer field; DMEM tag behavior is an explicit argument rather than assumed. Tests assert register programming for a two-chunk upload, reject malformed ranges, and force a stuck-queue timeout.

The caller must still parse the signed firmware descriptor into exact IMEM/DMEM source and destination ranges, reserve DMA memory, perform the correct SEC2/FWSEC/GSP boot ordering, and validate mailbox/status completion. This helper is hardware-facing transfer logic, not proof that firmware was authenticated or executed.


## Falcon reset and memory-scrub sequencing

Added `nvx_ga10x_falcon_reset()` for SEC2 and GSP. It checks the PRIV lock, asserts the documented engine-reset bit, waits 1 ms (conservatively longer than the 10 µs hardware reset pulse used by OpenRM/Nova), deasserts reset, then polls HWCFG2's memory-scrubbing bit with a bounded timeout. The implementation does not treat RESET_READY as mandatory because NVIDIA's GA102 reset path documents it as advisory. Host tests verify reset deassertion, scrub completion, and timeout behavior.

This is now the reset primitive needed before the DMA uploader. It still needs a verified platform transaction that orders SEC2 FWSEC loading, WPR/FRTS preparation, GSP booter load, RISC-V launch, and RPC initialization.


## Falcon boot completion and mailbox handshake

Added `nvx_ga10x_falcon_boot()`: it validates engine/BAR/output ranges, checks the privilege lock and halted state, writes optional input mailboxes, programs BOOTVEC and STARTCPU, polls CPUCTL.HALTED with a timeout, then returns both firmware mailboxes. Output values are cleared before the transaction and remain zero on failure. Host tests exercise input mailbox writes, a successful firmware-completion mailbox response, and a stuck-running timeout.

This follows the GA102 Falcon boot contract documented by Linux Nova/OpenRM. A returned mailbox pair is only a firmware-specific result; callers must decode the correct status for each SEC2/FWSEC or booter image. This still depends on correct firmware parsing, DMA upload, WPR setup, and the overall boot order.


## CPUCTL alias handling

The Falcon launch path now follows the documented GA102 `CPUCTL.ALIAS_EN` bit: when enabled, STARTCPU is issued through `CPUCTL_ALIAS` at engine-base + `0x130`; otherwise it uses CPUCTL directly. The alias-window bounds are checked before BOOTVEC is written; the full boot helper checks them before it writes either input mailbox, so a truncated mapping cannot leave partial boot arguments behind. Host tests cover direct and alias starts plus no-write failure behavior. This avoids assuming that the non-alias CPUCTL write is valid for every Falcon configuration.


## WPR metadata in DMA-visible SYSRAM

The boot-memory owner now allocates a fourth, dedicated 4 KiB contiguous DMA page for the 256-byte WPR metadata object. `nvx_gsp_boot_memory_prepare_wpr_metadata()` copies the platform-planned framebuffer layout, binds the actual physical SYSRAM addresses and bootloader descriptor offsets from the acquired artifacts, validates the resulting layout, serializes the exact V1 metadata structure into the DMA page, and returns that page's physical address for the Booter Load argument.

The serializer rejects a zero or non-page-aligned physical address, a short destination, pointer aliasing with the layout, and invalid/overlapping framebuffer layouts. It uses a release fence after copying. The DMA subsystem/platform remains responsible for any cache-maintenance operation required by its memory-coherency contract. The new allocation participates in rollback and pairwise physical-overlap checks with the runtime ELF, bootloader payload, and signature page.

This closes the **SYSRAM metadata allocation/materialization** step only. It does not program VRAM, execute Booter Load, authenticate FWSEC, or start the GSP. The physical framebuffer mapping and actual platform WPR inputs are still required.


## Fuse-selected Booter Load/Unload staging

The boot-memory owner now reserves separate, physically non-overlapping DMA buffers for both signed booter images in addition to the runtime ELF, GSP bootloader payload, signature section and WPR metadata page. Buffer sizes come from the fully preflighted image descriptors, not hard-coded constants.

`nvx_gsp_boot_memory_stage_booters_from_bar0()` delegates to the existing GA10x BAR0 fuse-read/preflight path to choose the correct signed booter payloads. On any failure it zeroes both staging allocations and clears both image descriptors; the staged flag is set only after both payloads succeed. This is the host-side handoff point immediately before SEC2/FWSEC execution, but it does not itself upload either image to Falcon IMEM/DMEM or run the signed firmware.

The strict-warning boot-memory test now exercises the whole ownership transaction with synthetic inputs: all six independent DMA artifacts, successful fuse-selected staging, injected staging failure/zeroization, WPR metadata preparation, and allocation failure on the final page with full rollback.


## Executable SEC2 Booter Load path

A new high-secure transfer planner translates the parsed GA10x booter descriptor into explicit Falcon operations. The offsets follow the published GA102 path: secure application code is DMAed from the payload's app-code offset into SEC2 IMEM at offset zero, OS data is DMAed from its payload offset into DMEM at offset zero, and the signature's DMEM-relative offset plus engine/ucode IDs are passed to BROM. The SEC2 boot vector remains the app-code offset.

`nvx_gsp_ga10x_execute_booter_load()` now sequences: SEC2 reset; explicit CPU-to-device ownership handoff for the signed booter; secure IMEM DMA; DMEM DMA; return ownership after transfer completion; BROM parameter programming; WPR metadata DMA ownership handoff; SEC2 start with the 64-bit WPR metadata SYSRAM address in mailboxes 0/1; wait for SEC2 halt; validate mailbox 0; and reclaim metadata ownership after the handoff completes. On transfer/boot failure it attempts a reset before reclaiming device-owned buffers. If reset cannot prove the engine quiescent, ownership is retained and boot-memory release refuses to free those buffers.

The strict-warning SEC2 test drives the actual MMIO helpers against a synthetic BAR0 register bank. It checks the transfer plan, reset/DMA/BROM/BOOTVEC/mailbox ordering and ownership transitions. This proves software sequencing only: synthetic MMIO cannot authenticate real firmware or establish that a physical GA102 GPU boots.


## Modern GSP-RM cached initialization arguments and shared queue layout

A dedicated ABI helper defines the 184-byte **modern** GA10x cached GSP init
argument structure with compile-time size/offset assertions. It plans the
contiguous SYSRAM region containing the PTE table, command queue, and status
queue using NVIDIA's public OpenRM page-count formula. The helper validates
physical alignment, allocation bounds, queue sizes, offsets, PTE counts, and
integer overflow; it can populate the self-mapping PTE array for the supported
single-page table profile and materialize a zero-filled 4 KiB argument page.

This is an ABI/planning layer, not an initialization claim. The `R535` RPC
header constant elsewhere in NVX does **not** identify the cached-init-argument
ABI. NVIDIA's R535-era `GSP_ARGUMENTS_CACHED` structure differs from the modern
structure implemented here, so the exact firmware branch must be selected
before wiring this helper into boot. The first profile supports only the
non-encrypted queue ABI; queue layouts requiring a multi-page PTE table are
rejected rather than guessed.

The platform must still allocate the contiguous DMA region, populate and flush
its memory as required by the platform coherency contract, pass the argument
page address through the actual GA10x boot path, and validate GSP launch and
GSP-RM initialization on physical hardware.

## Cached-init argument materialization guard

The modern cached-init argument materializer now rejects a zero status-queue
offset and rejects status-queue offsets that are not strictly after the command
queue offset. This is a structural consistency check on the serialized ABI
object: unlike the queue planner, the wire structure does not carry both queue
lengths, so materialization cannot independently prove that the two complete
queue ranges do not overlap. Callers must build arguments from a validated
queue layout rather than hand-editing the wire structure. Strict-warning host
tests cover zero, equal, and reversed offsets. This remains synthetic validation,
not evidence of a particular firmware branch accepting the ABI.


## Reverse-engineering notebook

For the detailed evidence trail and field-by-field contracts covering VBIOS pointer domains, FWSEC V3 descriptors, SB/FRTS command records, signature selection, SEC2 Booter Load ordering, DMA ownership, and unresolved physical-hardware requirements, see [NVX GA10x Firmware Reverse-Engineering Notebook](FIRMWARE_REVERSE_ENGINEERING.md).


## FWSEC command result contract

NVX now has a pure SB/FRTS result validator that checks the documented scratch error fields, GFW security/progress postconditions, and FRTS WPR2 address match. It accepts decoded fields from a platform reader and does no MMIO; it does not constitute command execution. See [the firmware reverse-engineering notebook](FIRMWARE_REVERSE_ENGINEERING.md).


## SEC2 transaction evidence trace

The traced Booter Load API exposes the current phase, last completed phase, failure phase, result, and recovery/quiescence outcome. This improves diagnosis without changing the transaction contract. See [SEC2 transaction trace](SEC2_TRANSACTION_TRACE.md). A completed synthetic trace is not proof of physical firmware authentication or boot.
