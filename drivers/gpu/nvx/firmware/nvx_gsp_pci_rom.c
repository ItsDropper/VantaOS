/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 *
 * Independently implemented bounded PCI expansion-ROM chain parser.
 */
#include "nvx_gsp_pci_rom.h"

#include <limits.h>
#include <string.h>

#define PCI_ROM_HEADER_BYTES 0x1AU
#define PCI_ROM_SIGNATURE_0  0x55U
#define PCI_ROM_SIGNATURE_1  0xAAU
#define PCIR_MIN_BYTES       0x18U
#define PCIR_IMAGE_LENGTH    0x10U
#define PCIR_CODE_TYPE       0x14U
#define PCIR_INDICATOR       0x15U
#define PCIR_LAST_IMAGE      0x80U
#define PCI_ROM_BLOCK_BYTES  512U

static int range_ok(size_t offset, size_t length, size_t limit)
{
    return offset <= limit && length <= limit - offset;
}

static int ranges_overlap(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x = (uintptr_t)a;
    uintptr_t y = (uintptr_t)b;

    if (an == 0U || bn == 0U)
        return 0;
    if (x > UINTPTR_MAX - an || y > UINTPTR_MAX - bn)
        return 1;
    return x < y + bn && y < x + an;
}

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static uint8_t checksum8(const uint8_t *bytes, size_t size)
{
    uint8_t sum = 0U;
    size_t i;

    for (i = 0U; i < size; ++i)
        sum = (uint8_t)(sum + bytes[i]);
    return sum;
}

nvx_gsp_pci_rom_status_t nvx_gsp_pci_rom_next_image(
    const void *rom_data, size_t rom_size, size_t cursor,
    nvx_gsp_pci_rom_image_t *out_image, size_t *out_next_cursor)
{
    const uint8_t *rom = (const uint8_t *)rom_data;
    nvx_gsp_pci_rom_image_t image;
    size_t pcir_relative;
    size_t pcir_offset;
    size_t image_size;
    size_t image_end;
    uint16_t image_blocks;
    uint16_t pcir_length;
    uint32_t pcir_signature;

    if (!rom || !out_image || !out_next_cursor || cursor >= rom_size)
        return NVX_GSP_PCI_ROM_INVALID_ARGUMENT;
    if (ranges_overlap(out_image, sizeof(*out_image), rom, rom_size) ||
        ranges_overlap(out_next_cursor, sizeof(*out_next_cursor), rom, rom_size) ||
        ranges_overlap(out_image, sizeof(*out_image), out_next_cursor,
                       sizeof(*out_next_cursor)))
        return NVX_GSP_PCI_ROM_INVALID_ARGUMENT;

    memset(out_image, 0, sizeof(*out_image));
    *out_next_cursor = 0U;
    if (!range_ok(cursor, PCI_ROM_HEADER_BYTES, rom_size))
        return NVX_GSP_PCI_ROM_RANGE_ERROR;
    if (rom[cursor] != PCI_ROM_SIGNATURE_0 ||
        rom[cursor + 1U] != PCI_ROM_SIGNATURE_1)
        return NVX_GSP_PCI_ROM_MALFORMED;

    pcir_relative = (size_t)read_le16(rom + cursor + 0x18U);
    if (pcir_relative < PCI_ROM_HEADER_BYTES ||
        !range_ok(cursor, pcir_relative, rom_size) ||
        !range_ok(cursor + pcir_relative, PCIR_MIN_BYTES, rom_size))
        return NVX_GSP_PCI_ROM_RANGE_ERROR;
    pcir_offset = cursor + pcir_relative;
    pcir_signature = read_le32(rom + pcir_offset);
    if (pcir_signature != 0x52494350U) /* "PCIR" little-endian */
        return NVX_GSP_PCI_ROM_MALFORMED;

    pcir_length = read_le16(rom + pcir_offset + 0x0AU);
    if (pcir_length < PCIR_MIN_BYTES ||
        !range_ok(pcir_offset, pcir_length, rom_size))
        return NVX_GSP_PCI_ROM_MALFORMED;

    image_blocks = read_le16(rom + pcir_offset + PCIR_IMAGE_LENGTH);
    if (image_blocks == 0U)
        return NVX_GSP_PCI_ROM_MALFORMED;
    image_size = (size_t)image_blocks * PCI_ROM_BLOCK_BYTES;
    if (cursor > SIZE_MAX - image_size)
        return NVX_GSP_PCI_ROM_RANGE_ERROR;
    image_end = cursor + image_size;
    if (!range_ok(cursor, image_size, rom_size) ||
        !range_ok(pcir_offset, pcir_length, image_end))
        return NVX_GSP_PCI_ROM_RANGE_ERROR;

    image.image_offset = cursor;
    image.image_size = image_size;
    image.pcir_offset = pcir_offset;
    image.vendor_id = read_le16(rom + pcir_offset + 4U);
    image.device_id = read_le16(rom + pcir_offset + 6U);
    image.code_type = rom[pcir_offset + PCIR_CODE_TYPE];
    image.indicator = rom[pcir_offset + PCIR_INDICATOR];
    image.checksum_valid = (uint8_t)(checksum8(rom + cursor, image_size) == 0U);

    *out_image = image;
    *out_next_cursor = image_end;
    return NVX_GSP_PCI_ROM_OK;
}

nvx_gsp_pci_rom_status_t nvx_gsp_pci_rom_find_nvidia(
    const void *rom_data, size_t rom_size, nvx_gsp_pci_rom_image_t *out_image)
{
    const uint8_t *rom = (const uint8_t *)rom_data;
    size_t cursor = 0U;

    if (!rom || !out_image)
        return NVX_GSP_PCI_ROM_INVALID_ARGUMENT;
    if (ranges_overlap(out_image, sizeof(*out_image), rom, rom_size))
        return NVX_GSP_PCI_ROM_INVALID_ARGUMENT;
    memset(out_image, 0, sizeof(*out_image));

    while (cursor < rom_size) {
        nvx_gsp_pci_rom_image_t image;
        size_t next_cursor = 0U;
        nvx_gsp_pci_rom_status_t status = nvx_gsp_pci_rom_next_image(
            rom, rom_size, cursor, &image, &next_cursor);

        if (status != NVX_GSP_PCI_ROM_OK)
            return status;
        if (image.vendor_id == NVX_GSP_PCI_ROM_VENDOR_NVIDIA) {
            *out_image = image;
            return NVX_GSP_PCI_ROM_OK;
        }
        if ((image.indicator & PCIR_LAST_IMAGE) != 0U)
            return NVX_GSP_PCI_ROM_NOT_FOUND;
        if (next_cursor <= cursor || next_cursor >= rom_size)
            return NVX_GSP_PCI_ROM_MALFORMED;
        cursor = next_cursor;
    }

    return NVX_GSP_PCI_ROM_NOT_FOUND;
}
