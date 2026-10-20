# NVX GA10x Firmware Reverse-Engineering Notebook

**Target:** NVIDIA GA106 / GA10x, initially GeForce RTX 3060  
**Scope:** VBIOS FWSEC discovery, signed SEC2 Booter Load, WPR handoff, and the boundary between the SEC2 stage and GSP runtime startup.

This document is a durable engineering record of the format/sequence reconstruction used by NVX. It deliberately distinguishes facts visible in published source, conclusions independently encoded and tested in NVX, and claims that still require a physical GA10x GPU. It is not a claim of complete NVIDIA compatibility.

## 1. Evidence labels and method

Use these labels in code reviews and future documentation:

- **Published contract** — a field, register, branch, or sequence is explicitly present in a public NVIDIA source file or public register definition. Record the exact source path.
- **Independent NVX interpretation** — NVX implements the contract independently and synthetic tests exercise it. Tests establish software behavior for supplied bytes/register values, not the truth of those values on hardware.
- **Inference** — the behavior is derived by connecting multiple fields or call sites, but the available public contract does not fully specify it.
- **Hardware pending** — requires a physical target and real firmware response. Never promote it to “working” based on a fake BAR mapping, host test, compile, or successful register write.

Research sources used for this pass:

1. NVIDIA Open GPU Kernel Modules, MIT-licensed:
   - `src/nvidia/src/kernel/gpu/gsp/kernel_gsp_fwsec.c` — BIT traversal and FWSEC VBIOS descriptor parsing.
   - `src/nvidia/src/kernel/gpu/gsp/kernel_gsp_booter.c` — booter image preparation, signature patching, and SEC2 high-secure boot data flow.
   - `src/nvidia/src/kernel/gpu/gsp/arch/turing/kernel_gsp_frts_tu102.c` — FWSEC interface patching, fuse-selected VBIOS signature selection, and FRTS/SB command structures.
   - GA102 Falcon HAL/register definitions — engine-specific registers and ordering.
2. Linux Nova register/HAL definitions are useful as an independent cross-check for offsets and bit semantics, not as a source to copy into NVX.
3. NVX synthetic tests establish that our parser and sequencing helpers reject or accept the test vectors we authored. They do not establish hardware acceptance.

No Nouveau implementation is used as implementation source. Public sources are research evidence; NVX code is independently written.

## 2. Address domains in the VBIOS parser

The most error-prone detail is that these pointers do **not** all use the same base.

| Object | How it is located | Address domain |
| --- | --- | --- |
| BIT header | Scan candidate offsets for ID `0xB8FF`, signature `BIT\0`, then validate header checksum | Absolute offset in the supplied VBIOS byte buffer |
| BIT token table | `BIT offset + HeaderSize + token_index * TokenSize` | Absolute buffer offset |
| Falcon-data token's `DataPtr` | Read the 4-byte Falcon-data structure at `DataPtr` | Absolute buffer offset |
| `FalconUcodeTablePtr` inside Falcon-data v2 | Add it to the expansion-ROM image offset | Relative to the expansion-ROM image |
| Falcon table entry's `DescPtr` | Add it to the expansion-ROM image offset | Relative to the expansion-ROM image |
| FWSEC signed image | `descriptor offset + descriptor size` | Absolute buffer offset after resolving the descriptor |

**Finding:** applying the expansion-ROM base to the BIT token's `DataPtr` is wrong. The expansion-ROM base is added only to the Falcon-table pointer and descriptor pointer. NVIDIA's parser does these operations separately in `kernel_gsp_fwsec.c`; NVX mirrors the pointer-domain distinction with checked additions.

The BIT header is not trusted just because the marker bytes occur in the ROM. The candidate must fit in the supplied image and its declared header checksum must sum to zero modulo 256. The declared token size selects the 8-byte token layout when available, otherwise the 6-byte layout. Token iteration must be bounded by `TokenEntries * TokenSize`, with multiplication and offset overflow checked before reading.

The Falcon table has a version, header size, entry size, and entry count. NVX requires the supported table version and minimum sizes, then bounds-checks the complete entry array before traversing it. Application IDs matter: production FWSEC is `0x85`, debug FWSEC is `0x45`, and the licensing entry is `0x05`. The current NVX parser deliberately selects production `0x85` only; it must not silently fall back to debug firmware.

## 3. Production FWSEC V3 descriptor layout

The public NVIDIA definition declares a packed 44-byte fixed V3 prefix. The descriptor header's low bits include availability/encryption flags, bits 15:8 carry the descriptor version, and bits 31:16 carry the total descriptor size. The size includes the signature array appended after the 44-byte fixed prefix.

| Offset from descriptor | Field | Size | Interpretation / validation |
| ---: | --- | ---: | --- |
| 0 | descriptor header | 4 | available flag, encrypted flag, version, total descriptor size |
| 4 | `StoredSize` | 4 | stored signed-image bytes; image starts after the entire descriptor |
| 8 | `PKCDataOffset` | 4 | destination offset for the selected 384-byte RSA-3072 signature inside DMEM |
| 12 | `InterfaceOffset` | 4 | firmware application-interface metadata offset |
| 16 | `IMEMPhysBase` | 4 | Falcon IMEM destination base |
| 20 | `IMEMLoadSize` | 4 | IMEM bytes to load |
| 24 | `IMEMVirtBase` | 4 | firmware virtual IMEM base |
| 28 | `DMEMPhysBase` | 4 | Falcon DMEM destination base |
| 32 | `DMEMLoadSize` | 4 | DMEM bytes to load |
| 36 | `EngineIdMask` | 2 | target Falcon engine mask |
| 38 | `UcodeId` | 1 | firmware version/fuse array selector |
| 39 | `SignatureCount` | 1 | number of appended signatures |
| 40 | `SignatureVersions` | 2 | bitmap of supported fuse versions |
| 42 | reserved | 2 | must be zero for the supported profile |
| 44 onward | signatures | `SignatureCount * 384` | packed RSA-3072 signatures |

NVX rejects unknown descriptor versions, unavailable/reserved flags, nonzero reserved fields, a wrong SEC2 engine mask, invalid ucode IDs, zero or inconsistent sizes, malformed signature counts, and out-of-range image extents. It supports the V3 production descriptor only; V2 and debug paths are intentionally not inferred into this parser.

The runtime image begins at `descriptor_offset + descriptor_size`, not immediately after the 44-byte prefix. Its allocated transfer extent is rounded up to 256 bytes, as NVIDIA's code does when allocating the high-secure firmware image. NVX checks that rounded extent against the supplied ROM buffer.

### 3.1 Signature-version bitmap mapping

**Published contract:** the V3 descriptor contains a bitmap of supported firmware fuse versions and a packed array of signatures. NVIDIA's `kernel_gsp_frts_tu102.c` selects the signature matching `1 << fuse_version`, then counts the set bits for supported versions below that bit to compute the packed signature offset.

**Independent NVX interpretation:** `nvx_gsp_vbios_fwsec_signature_offset()` first requires the bitmap population count to equal `SignatureCount`, rejects fuse versions outside the 16-bit bitmap or absent from the bitmap, and returns:

`signatures_offset + popcount(signature_versions & ((1 << fuse_version) - 1)) * 384`

The implementation uses guarded shifts and a wide intermediate for the final offset. For bitmap `0x0025` (versions 0, 2, and 5), fuse version 5 selects signature index 2. This is a different selection scheme from the separate booter-container signature array, so the two must not share an unqualified “signature index” helper.

### 3.2 Image layout and DMEM-relative signature address

NVIDIA's FWSEC descriptor handling sets the data region offset to `IMEMLoadSize`, then checks `DMEMLoadSize` against the remainder of the rounded image allocation. The PKC signature destination is relative to DMEM, and must leave room for one full 384-byte signature within the DMEM region. NVX's transfer plan preserves the descriptor's signature destination as a DMEM-relative offset; the Falcon BROM parameter register receives that offset, not an address into the host VBIOS buffer.

The descriptor's `InterfaceOffset` is separate from the signature destination. It is needed later to find the FWSEC application interface used to prepare firmware commands; parsing it does not mean the interface has been validated or executed.

## 3.3 FWSEC application interface and command input buffer

The public TU102 FWSEC code defines an interface header as four one-byte fields: version, header size, entry size, and entry count. Each interface entry begins with two 32-bit fields: entry ID and DMEM offset. Entry ID `0x4` identifies the DMEM mapper. The mapper's V3 fixed prefix is 64 bytes; its leading fields are a 32-bit signature, 16-bit version, 16-bit structure size, then 32-bit command-input offset and capacity.

The new read-only NVX helper `nvx_gsp_fwsec_find_command_window()` resolves the descriptor's `InterfaceOffset` against the DMEM buffer and returns a checked command-input window. It validates the header version, minimum header/entry sizes, full declared entry array, mapper uniqueness, mapper version/size, and the complete command-input buffer range. It refuses ambiguous duplicate mapper entries rather than silently choosing one. It also rejects a command-input range that overlaps the mapper structure or the interface table, because later command patching must not destroy the metadata used to locate that command buffer. All returned offsets are DMEM-relative.

A subtle boundary matters here: the mapper's `cmd_in_buffer_offset` is an offset from the start of DMEM, not an offset relative to the mapper structure or the interface table. Bounds checks must validate `cmd_in_buffer_offset + command_size` against the full DMEM extent and ensure `command_size <= cmd_in_buffer_size`. The planner does not write `init_cmd`, patch the interface, or transfer DMA ownership; those operations belong in a later transaction layer after the parser's result is validated.

**Scope limitation:** NVX currently supports interface header version 1 and DMEM mapper version 3 only. The parser treats the mapper signature as opaque and does not infer a command's success semantics from the interface layout. SB/FRTS command construction, firmware result decoding, and physical SEC2 execution remain separate tasks.

## 4. Do not conflate the two signed firmware formats

NVX has two related but distinct parsers/paths:

### A. FWSEC from VBIOS

- Found by BIT token `0x70`, Falcon-data version 2, then Falcon table and production FWSEC descriptor.
- Uses V3's `SignatureVersions` bitmap and the matching fuse version to select a signature.
- Image begins after the descriptor, whose declared size includes appended signatures.
- Runs as a high-secure FWSEC stage on SEC2 and is involved in establishing the secure firmware/WPR state.

### B. Booter Load/Unload bundle image

- Comes from the acquired GSP firmware bundle, not the VBIOS BIT table.
- The extracted booter format has its own header, application descriptor, patch location, patch-signature field, signature count, and signature array.
- NVIDIA's `kernel_gsp_booter.c` selects the booter signature using `num_sigs - 1 - fuse_version` for this format and rejects nonzero `patch_sig`.
- The selected signature is patched into the booter payload at the descriptor's patch location. This is signature **selection and placement**, not host-side cryptographic verification.

The formulas look different because the data formats encode supported versions differently. Do not “normalize” them into one selector unless the format is an explicit parameter and each mapping has separate test vectors.

## 5. SEC2 Booter Load: transaction reconstructed so far

The current NVX high-secure transaction follows this bounded software sequence:

1. Validate that boot memory owns the staged Booter Load image and prepared WPR metadata page.
2. Convert the parsed descriptor into an explicit transfer plan. The current supported path requires SEC2 engine mask `0x0001`, one application, aligned IMEM/DMEM extents, a valid signature patch location, and a valid ucode ID.
3. Reset SEC2 and wait for the memory-scrub state to complete with a finite timeout.
4. Transfer DMA ownership of the staged booter to the device.
5. Upload application code to SEC2 IMEM at destination offset zero using the secure IMEM DMA mode.
6. Upload OS data to SEC2 DMEM at destination offset zero.
7. Wait for transfer completion, then return ownership of the booter staging buffer to the CPU.
8. Program the SEC2 Falcon BROM parameters: signature DMEM offset, engine mask, ucode ID, and RSA-3K algorithm selector.
9. Transfer ownership of the WPR metadata page to the device.
10. Start SEC2 at the descriptor-derived boot vector. Mailboxes 0 and 1 carry the low and high 32-bit halves of the WPR metadata SYSRAM physical address in this NVX path.
11. Wait for SEC2 to halt, capture both mailboxes, return metadata-page ownership to the CPU, and treat nonzero mailbox 0 as a reported boot error. Mailbox 1 is returned to the caller as diagnostic/result data.

The engine windows are not interchangeable: SEC2's Falcon window is based at `0x840000`; the GSP Falcon window is based at `0x110000`. The engine-aware helpers validate SEC2 base pairs and the SEC2 engine mask to avoid programming GSP registers for a SEC2 descriptor.

### Failure and ownership rules

DMA buffers must not be freed while the device may still own them. After a transfer or boot failure, NVX attempts a SEC2 reset before reclaiming ownership. If reset cannot establish a quiescent state, ownership remains with the device and boot-memory release refuses to free the affected allocations. This is intentionally fail-closed; a leaked allocation is preferable to use-after-free by active firmware.

The host test's delay callback mutates a synthetic BAR to simulate idle DMA, a halted Falcon, and mailbox values. It verifies ordering/ownership code paths, but **does not simulate SEC2 firmware, the BROM, signature verification, or a GPU**. A synthetic mailbox success is only the test harness's chosen value.

## 6. WPR and the still-missing bridge to runtime GSP

NVX has CPU-side planning and SYSRAM artifacts for a future WPR handoff: the validated runtime ELF, bootloader payload, runtime signature section, booter load/unload images, and a dedicated DMA-visible WPR metadata page. The framebuffer layout planner aligns ranges and checks overlaps using caller-supplied hardware/platform inputs.

Still required before a genuine runtime launch:

- Read and validate the actual physical framebuffer size and VBIOS workspace on the target.
- Obtain any MMU-lock lower bound and determine the secure FRTS region through the firmware-defined command path.
- Derive monitor code/data/manifest offsets from the matching bootloader descriptor and validate all ranges against the exact payload.
- Establish a supported platform heap policy and prove every WPR subrange fits installed/usable VRAM.
- Implement the full VBIOS FWSEC extraction, image preparation, signature selection, FWSEC interface patching, and SEC2 command/result handling.
- Implement the vendor-defined FRTS and secure-boot command sequence and verify all scratch/mailbox status meanings for the exact firmware generation.
- Allocate/map actual framebuffer memory, write the WPR metadata at the firmware-required location, and observe hardware-confirmed completion.
- Execute the required Booter Load/Unload and GSP boot sequence with real DMA addresses and hardware timeouts.
- Validate the exact GSP-RM cached-init ABI for the firmware branch. The modern 184-byte cached-init profile must not be silently combined with the R535-era ABI.
- Complete the first real GSP-RM RPC and verify the runtime's response before marking the device initialized.
- Only then implement and validate GPU VM/channel/object setup, command submission, interrupts/fences, memory management, and graphics API integration.

Do not skip secure boot and jump straight to rendering. A GPU command submission path is meaningless if firmware trust, VRAM layout, GSP-RM initialization, and queue ABI are not established.

## 7. High-priority unknowns and how to resolve them

| Unknown / risk | Evidence needed | Safe next experiment |
| --- | --- | --- |
| Exact SEC2 Booter Load mailbox 0/1 meanings across firmware versions | Matching NVIDIA call sites and firmware-version-specific result checks | Add named status decoding only after locating the exact contract; keep unknown values as errors |
| VBIOS expansion-ROM chain selection | PCI ROM header/PCIR image-length and last-image semantics | Implement a separate bounded ROM-image locator and tests; do not pass an arbitrary offset to the FWSEC parser |
| FWSEC image encryption/compression behavior | Descriptor flags and exact GA10x firmware path | Fail closed for unsupported flags; never interpret encrypted bytes as a plain executable image |
| FWSEC application-interface ABI | Public interface header/entry layout and DMEM mapper version | Parse with checked offsets and sizes; validate command input buffer offsets against DMEM, not just the whole firmware image |
| FRTS/WPR layout inputs | Firmware commands, real FB size/workspace/lock state and hardware readback | Add read-only diagnostics and pure planners before writes |
| Modern cached-init ABI version selection | Runtime firmware build metadata or authoritative matching-branch contract | Tie each ABI profile to explicit firmware identity; reject unknown profiles |
| Actual device behavior | RTX 3060 / GA106, real VBIOS and GSP firmware, serial/kernel logs and register snapshots | Use staged read-only diagnostics first, then one transaction at a time with recovery and power-cycle plan |

## 8. Driver work order

1. Keep PR #159 buildable and make the host tests pass before extending the SEC2 path.
2. Add a standalone bounded expansion-ROM/PCIR locator; keep it separate from the BIT/FWSEC parser.
3. Add a pure FWSEC command-interface parser for the public interface table/entry/DMEM mapper, with malformed-offset, overflow, overlapping-range, and no-write-on-failure tests.
4. Add a versioned FWSEC command builder for the documented SB/FRTS structures only after validating exact sizes, field widths, and status semantics.
5. Add explicit runtime firmware identity and cached-init ABI profile selection.
6. Integrate real hardware reads/writes only after the pure parsers and planners have exhaustive synthetic tests.
7. Do not report hardware boot or acceleration until a physical target completes the transaction and a real GSP-RM RPC.

## 9. Source links

- [NVIDIA Open GPU Kernel Modules — FWSEC VBIOS parser](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/src/nvidia/src/kernel/gpu/gsp/kernel_gsp_fwsec.c)
- [NVIDIA Open GPU Kernel Modules — Booter loading and signature patching](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/src/nvidia/src/kernel/gpu/gsp/kernel_gsp_booter.c)
- [NVIDIA Open GPU Kernel Modules — TU102+ FWSEC interface and FRTS/SB commands](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/src/nvidia/src/kernel/gpu/gsp/arch/turing/kernel_gsp_frts_tu102.c)
- [NVIDIA Open GPU Kernel Modules — GA102 GSP HAL](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/src/nvidia/src/kernel/gpu/gsp/arch/ampere/kernel_gsp_ga102.c)
- [Linux Nova Falcon/GSP register definitions](https://github.com/torvalds/linux/tree/master/drivers/gpu/nova-core)

This notebook should evolve alongside the code. Every new register, structure, or status code should include: source evidence, address/offset domain, range and alignment constraints, firmware-generation scope, synthetic tests, and an explicit hardware-validation status.


## 3.4 Version-1 SB and FRTS command serialization

Cross-checking the actual structure declarations and preparation path in NVIDIA's MIT-licensed `kernel_gsp_frts_tu102.c` established the command IDs and exact record sizes used by the supported profile:

| Command | ID | Input record | Size |
| --- | ---: | --- | ---: |
| Secure boot (SB) | `0x19` | `FWSECLIC_READ_VBIOS_DESC` | 24 bytes |
| FRTS setup | `0x15` | `FWSECLIC_FRTS_CMD` containing the read-VBIOS and FRTS region descriptors | 44 bytes |

The read-VBIOS descriptor is five fields in its declared order: 32-bit version (1), 32-bit size (24), 64-bit GFW image offset (0), 32-bit image size (0), and 32-bit flags (2). The FRTS region descriptor is five 32-bit fields: version (1), size (20), framebuffer offset in 4 KiB units, region size (0x100 units, i.e. 1 MiB), and media type (2 = framebuffer). The FRTS input offset must be nonzero; NVX additionally requires 4 KiB alignment and verifies the shifted offset fits in the wire field.

NVX now builds these records as explicit little-endian byte sequences in `nvx_gsp_fwsec_build_command()`. The builder is pure: it neither modifies FWSEC DMEM nor copies the selected signature, changes DMA ownership, writes registers, or starts SEC2. It validates command IDs, output capacity, offset alignment/range, and output/size aliasing before publishing bytes. Strict-warning tests check exact field offsets and that failure leaves the destination untouched.

**Boundary:** the builder prepares command bytes only. It does not decode scratch-register error fields, prove the target firmware accepts this exact profile, execute SB/FRTS, or authenticate FWSEC. The command-status contract and firmware-generation binding still need to be researched before adding a transaction executor.


## 3.5 SB/FRTS result validation reconstructed from the public execution path

The published TU102+ result path provides explicit postconditions rather than treating Falcon halt as success:

- **FRTS (`0x15`):** the error code is the upper 16 bits of VBIOS scratch index `0x0E` and must be zero; WPR2 high-address field must be nonzero; the decoded WPR2 low-address field must equal the requested FRTS framebuffer offset transformed by the hardware-defined `NV_PFB_PRI_MMU_WPR2_ADDR_LO_ALIGNMENT` shift.
- **SB (`0x19`):** the secure-scratch PLM state must report GFW read-protection level 0 enabled; GFW progress must report completed; the low 16 bits of VBIOS scratch index `0x15` must be zero.

The independent `nvx_gsp_fwsec_validate_result()` helper implements those decision branches in the same fail-fast order, but accepts an already-decoded snapshot. It intentionally does not guess the WPR2 alignment shift, read registers, clear scratch fields, or execute firmware. The platform must decode the register fields from the matching GA10x definitions and supply a coherent snapshot. Synthetic tests cover every failure branch and success.

This closes the **result interpretation** layer only. A correct decoded snapshot from fake BAR memory remains a test fixture, not evidence that SEC2 ran. Real execution still needs a firmware-generation-bound transaction, real VBIOS/FWSEC image, DMA-coherent staging, physical WPR memory, and actual hardware readback.
