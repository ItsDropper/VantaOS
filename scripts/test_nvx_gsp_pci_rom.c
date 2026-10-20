/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_pci_rom.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ROM_BYTES 1536U
#define PCIR_OFFSET 0x40U

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U);
    p[3] = (uint8_t)(value >> 24U);
}

static void make_image(uint8_t *rom, size_t offset, uint16_t blocks,
                       uint16_t vendor, uint16_t device, uint8_t code_type,
                       uint8_t indicator)
{
    uint8_t *pcir = rom + offset + PCIR_OFFSET;
    rom[offset] = 0x55U;
    rom[offset + 1U] = 0xAAU;
    put16(rom + offset + 0x18U, PCIR_OFFSET);
    put32(pcir, 0x52494350U);
    put16(pcir + 4U, vendor);
    put16(pcir + 6U, device);
    put16(pcir + 0x0AU, 0x18U);
    put16(pcir + 0x10U, blocks);
    pcir[0x14U] = code_type;
    pcir[0x15U] = indicator;
}

int main(void)
{
    uint8_t rom[ROM_BYTES];
    nvx_gsp_pci_rom_image_t image;
    size_t next = 0U;

    memset(rom, 0, sizeof(rom));
    make_image(rom, 0U, 1U, 0x1234U, 0x5678U,
               NVX_GSP_PCI_ROM_CODE_X86, 0U);
    make_image(rom, 512U, 2U, NVX_GSP_PCI_ROM_VENDOR_NVIDIA, 0x2684U,
               NVX_GSP_PCI_ROM_CODE_EFI, 0x80U);

    assert(nvx_gsp_pci_rom_next_image(rom, sizeof(rom), 0U,
        &image, &next) == NVX_GSP_PCI_ROM_OK);
    assert(image.image_offset == 0U);
    assert(image.image_size == 512U);
    assert(image.pcir_offset == PCIR_OFFSET);
    assert(image.vendor_id == 0x1234U);
    assert(next == 512U);
    assert((image.indicator & 0x80U) == 0U);

    assert(nvx_gsp_pci_rom_find_nvidia(rom, sizeof(rom), &image) ==
           NVX_GSP_PCI_ROM_OK);
    assert(image.image_offset == 512U);
    assert(image.image_size == 1024U);
    assert(image.pcir_offset == 512U + PCIR_OFFSET);
    assert(image.vendor_id == NVX_GSP_PCI_ROM_VENDOR_NVIDIA);
    assert(image.device_id == 0x2684U);
    assert(image.code_type == NVX_GSP_PCI_ROM_CODE_EFI);
    assert((image.indicator & 0x80U) != 0U);

    assert(nvx_gsp_pci_rom_find_nvidia(rom, 512U, &image) ==
           NVX_GSP_PCI_ROM_MALFORMED);

    memset(rom, 0, sizeof(rom));
    make_image(rom, 0U, 1U, NVX_GSP_PCI_ROM_VENDOR_NVIDIA, 0x2684U,
               NVX_GSP_PCI_ROM_CODE_X86, 0x80U);
    put16(rom + 0x18U, 0xFFF0U);
    assert(nvx_gsp_pci_rom_next_image(rom, sizeof(rom), 0U,
        &image, &next) == NVX_GSP_PCI_ROM_RANGE_ERROR);

    memset(rom, 0, sizeof(rom));
    make_image(rom, 0U, 0U, NVX_GSP_PCI_ROM_VENDOR_NVIDIA, 0x2684U,
               NVX_GSP_PCI_ROM_CODE_X86, 0x80U);
    assert(nvx_gsp_pci_rom_next_image(rom, sizeof(rom), 0U,
        &image, &next) == NVX_GSP_PCI_ROM_MALFORMED);

    assert(nvx_gsp_pci_rom_next_image(rom, sizeof(rom), sizeof(rom),
        &image, &next) == NVX_GSP_PCI_ROM_INVALID_ARGUMENT);
    assert(nvx_gsp_pci_rom_next_image(NULL, sizeof(rom), 0U,
        &image, &next) == NVX_GSP_PCI_ROM_INVALID_ARGUMENT);

    puts("NVX PCI expansion-ROM chain tests passed");
    return 0;
}
