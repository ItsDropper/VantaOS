# NVX PCI expansion-ROM chain parser

**Target:** safely locate a vendor image inside a PCI expansion-ROM byte buffer before parsing NVIDIA BIT/FWSEC metadata.  
**Implementation:** `include/nvx_gsp_pci_rom.h`, `drivers/gpu/nvx/nvx_gsp_pci_rom.c`.  
**Validation status:** strict-warning host tests are added; physical VBIOS parsing remains pending.

## Why this parser exists

A PCI expansion ROM may contain a chain of images, for example a legacy x86 option ROM followed by an EFI image. Treating the entire byte buffer as one image makes offsets ambiguous and can cause the FWSEC parser to use the wrong expansion-ROM base. The existing VBIOS parser explicitly distinguishes absolute BIT token pointers from Falcon table/descriptor pointers relative to the selected expansion-ROM image.

This parser establishes the image boundary first. Its returned `image_offset` is suitable as the `expansion_rom_offset` argument to `nvx_gsp_vbios_find_ga10x_fwsec()`.

## Parsed structure

For each image at a candidate cursor, NVX checks:

1. The `0x55, 0xAA` expansion-ROM signature.
2. The 16-bit PCIR pointer at image offset `0x18`.
3. The PCIR signature bytes `PCIR`, decoded explicitly as little-endian.
4. The declared PCIR structure length and that the full structure lies within both the supplied buffer and the declared image.
5. A nonzero 16-bit image length in 512-byte units.
6. Overflow-safe image-size multiplication/addition and the complete image extent.
7. The vendor/device IDs, code type, and last-image indicator.

The chain walker advances by the declared image size. It returns the first image whose PCIR vendor ID is NVIDIA (`0x10DE`). It stops with `NOT_FOUND` only when a valid final image is reached without a match; a chain that claims another image but runs out of bytes is malformed.

## Important design choices

- **No arbitrary scan for a later PCIR signature.** The next image starts at the previous image's declared end, so malformed data cannot trick the parser into accepting an unrelated `PCIR` byte sequence elsewhere in the buffer.
- **No forced legacy/EFI preference yet.** The locator returns the first structurally valid NVIDIA-vendor image. The caller receives the code type and can apply a firmware-specific selection policy later.
- **Checksum is advisory.** `checksum_valid` reports whether the byte sum of the declared image is zero modulo 256; it is not used to reject the image. A later policy must account for code type and real VBIOS behavior before making checksum validity a hard requirement.
- **No cryptographic trust is inferred.** PCIR vendor ID, device ID, a valid checksum, and a bounded image are structural metadata only. None authenticates firmware.
- **No hidden allocation or MMIO.** The parser reads a supplied byte buffer, uses caller-provided outputs, and rejects output aliasing with the source buffer.

## Test matrix

The host test constructs a two-image chain and verifies that the second image is located at offset 512 with its 1024-byte declared extent. It also covers a chain that is truncated before its next image, an out-of-range PCIR pointer, a zero image length, invalid cursor, and null input.

These tests validate parser logic against synthetic bytes. A hardware validation pass still needs an actual RTX 3060 VBIOS dump, verified PCI ROM length, and a comparison between the selected image and the firmware identity used by the subsequent FWSEC parser.

## Next steps

1. Test with multiple real VBIOS dumps, including ROMs with both x86 and EFI images.
2. Decide the exact image-selection policy for GA106 using matching firmware/driver evidence, rather than assuming the first NVIDIA image is always the desired one.
3. Feed the selected image's offset into the existing BIT/FWSEC parser and test the absolute-versus-relative pointer domains end-to-end.
4. Only after structural parsing is validated, connect the result to the SEC2 FWSEC transaction.
