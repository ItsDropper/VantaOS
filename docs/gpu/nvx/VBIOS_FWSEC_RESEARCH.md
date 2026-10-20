# NVX GA10x VBIOS FWSEC metadata research

## Scope

NVX now has a bounded, read-only parser for the production GA10x FWSEC VBIOS descriptor. It is an independent implementation based on NVIDIA's published Open GPU Kernel Modules source and the documented BIT/Falcon descriptor field layouts. It does not copy Nouveau code.

Primary reference: NVIDIA, MIT-licensed
[`kernel_gsp_fwsec.c`](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/src/nvidia/src/kernel/gpu/gsp/kernel_gsp_fwsec.c).
The same repository's GA102 HAL and Falcon register headers provide architecture-specific context.

## Reconstructed pointer chain

The parser keeps three address domains distinct:

1. The BIT header is located by scanning the supplied VBIOS image for the `0xB8FF` identifier and `BIT\0` signature, then validating the header checksum.
2. BIT token `DataPtr` is an absolute offset in the supplied VBIOS byte buffer. The Falcon-data token points to a four-byte field containing the Falcon ucode table pointer.
3. The Falcon ucode table pointer and each descriptor pointer are relative to the caller-supplied expansion-ROM base. The image bytes follow the V3 descriptor; the signature array follows the fixed 44-byte descriptor prefix.

This distinction is important: applying the expansion-ROM base to the BIT token pointer itself produces the wrong address.

## Validation performed

- Bounds-check the BIT header, token table, Falcon table header/entries, descriptor, appended signature array, and aligned image extent.
- Require the supported Falcon table version and minimum header/entry sizes.
- Select production FWSEC application ID `0x85`; never silently fall back to a debug image.
- Require an available V3 descriptor, reject reserved descriptor bits and reserved V3 fields, and validate the SEC2 engine mask.
- Validate IMEM/DMEM lengths against the stored image and ensure the PKC signature destination fits in the DMEM range.
- Require signature metadata to match the RSA-3072 signature width and declared signature count.
- Resolve the fuse-version signature slot from the descriptor bitmap. The selected slot is the count of supported-version bits below the hardware fuse version, matching the selection behavior documented in NVIDIA's public `kernel_gsp_frts_tu102.c`; absent fuse versions are rejected.
- Return offsets and parsed metadata only; the parser does not allocate DMA memory, authenticate signatures, patch firmware, or start SEC2.

## Explicit limitations

- Only the production V3 descriptor path is accepted. V2 descriptors, debug FWSEC, unfamiliar descriptor versions, and unsupported layouts fail closed.
- The caller must provide a correctly identified expansion-ROM base. This function does not discover or validate the PCI ROM/PCIR image chain.
- Parsed fields are untrusted until checked against device fuse state and firmware-specific policy. In particular, this parser does not establish cryptographic authenticity.
- Host tests use synthetic bytes. Physical GA10x VBIOS parsing, SEC2 execution, FWSEC-FRTS preparation, WPR allocation/programming, GSP startup, and hardware acceleration remain unverified.

## FWSEC interface and command-builder update

The interface parser and command builder are documented in [the GA10x firmware notebook](FIRMWARE_REVERSE_ENGINEERING.md). The V3 DMEM mapper fixed prefix is 64 bytes, not 68 bytes; the structure field declarations in NVIDIA's public source support 64 bytes. SB uses command ID `0x19` and a 24-byte read-VBIOS descriptor; FRTS uses command ID `0x15` and a 44-byte composite record. These builders only prepare bytes and do not execute firmware.

## Next hardware boundary

The signature-slot selector now implements the documented bitmap-to-array mapping, but it only returns an offset; it does not authenticate or copy the signature. The next step is to integrate the parsed FWSEC descriptor with the existing DMA ownership and Falcon upload abstractions only after the descriptor's signed-image extent and SEC2 mailbox result semantics are independently checked against the exact target VBIOS/firmware generation. Do not infer success from a register write or synthetic MMIO test.
