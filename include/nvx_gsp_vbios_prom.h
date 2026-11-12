/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_VBIOS_PROM_H
#define VANTA_NVX_GSP_VBIOS_PROM_H

#include <stddef.h>
#include <stdint.h>
#include "nvx_mmio.h"

/*
 * Read the VBIOS through the GPU's PROM window (BAR0 + 0x300000, NV_PROM_DATA),
 * following NVIDIA's kvbiosExtractVbiosFromRom_TU102: optional IFR header
 * (versions 1-3), PCI ROM image chain, base/extension image accounting. Only
 * aligned 32-bit reads are issued. Read-only; no register is written.
 *
 * Not verified on hardware. Whether the PROM window is readable on a given
 * board/boot state is exactly what `nvx vbios` is meant to find out.
 */
#define NVX_VBIOS_PROM_BASE      0x00300000U
#define NVX_VBIOS_PROM_MAX_SIZE  0x00100000U  /* kvbiosGetBaseBiosMaxSize, GA10x */

typedef enum nvx_gsp_vbios_prom_status {
    NVX_VBIOS_PROM_OK = 0,
    NVX_VBIOS_PROM_INVALID_ARGUMENT,
    NVX_VBIOS_PROM_MMIO_FAILED,
    NVX_VBIOS_PROM_BAD_OFFSET,
    NVX_VBIOS_PROM_NO_ROM_SIGNATURE,
    NVX_VBIOS_PROM_BAD_IFR,
    NVX_VBIOS_PROM_BAD_PCI_DATA,
    NVX_VBIOS_PROM_TOO_LARGE,
    NVX_VBIOS_PROM_BUFFER_TOO_SMALL
} nvx_gsp_vbios_prom_status_t;

/* Aligned dword reader; offset is relative to the PROM window start. */
typedef int (*nvx_vbios_prom_read32_fn)(void *context, uint32_t offset, uint32_t *out);

/* Core logic over an abstract reader (host-testable). */
nvx_gsp_vbios_prom_status_t nvx_gsp_vbios_read_with(
    nvx_vbios_prom_read32_fn read32, void *context,
    void *buffer, size_t capacity,
    size_t *out_size, size_t *out_expansion_rom_offset);

/* BAR0 convenience wrapper. out_expansion_rom_offset feeds
 * nvx_gsp_vbios_find_ga10x_fwsec() unchanged. */
nvx_gsp_vbios_prom_status_t nvx_gsp_vbios_read_from_prom(
    const nvx_mmio_region_t *bar0, void *buffer, size_t capacity,
    size_t *out_size, size_t *out_expansion_rom_offset);

#endif
