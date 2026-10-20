/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_PCI_ROM_H
#define VANTA_NVX_GSP_PCI_ROM_H

#include <stddef.h>
#include <stdint.h>

#define NVX_GSP_PCI_ROM_VENDOR_NVIDIA 0x10DEU
#define NVX_GSP_PCI_ROM_CODE_X86      0x00U
#define NVX_GSP_PCI_ROM_CODE_EFI      0x03U

typedef enum nvx_gsp_pci_rom_status {
    NVX_GSP_PCI_ROM_OK = 0,
    NVX_GSP_PCI_ROM_INVALID_ARGUMENT,
    NVX_GSP_PCI_ROM_NOT_FOUND,
    NVX_GSP_PCI_ROM_MALFORMED,
    NVX_GSP_PCI_ROM_RANGE_ERROR
} nvx_gsp_pci_rom_status_t;

typedef struct nvx_gsp_pci_rom_image {
    size_t image_offset;
    size_t image_size;
    size_t pcir_offset;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t code_type;
    uint8_t indicator;
    uint8_t checksum_valid;
} nvx_gsp_pci_rom_image_t;

/*
 * Parse one PCI expansion-ROM image at cursor. Offsets in the output are
 * absolute offsets into rom. out_next_cursor points just past this image.
 * This is a bounded structural parser; checksum_valid is advisory only.
 */
nvx_gsp_pci_rom_status_t nvx_gsp_pci_rom_next_image(
    const void *rom, size_t rom_size, size_t cursor,
    nvx_gsp_pci_rom_image_t *out_image, size_t *out_next_cursor);

/*
 * Walk a chained PCI option ROM and return the first structurally valid
 * NVIDIA-vendor image. The result's image_offset can be passed directly as
 * expansion_rom_offset to nvx_gsp_vbios_find_ga10x_fwsec().
 */
nvx_gsp_pci_rom_status_t nvx_gsp_pci_rom_find_nvidia(
    const void *rom, size_t rom_size, nvx_gsp_pci_rom_image_t *out_image);

#endif
