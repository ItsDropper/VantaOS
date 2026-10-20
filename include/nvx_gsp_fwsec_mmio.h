/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_FWSEC_MMIO_H
#define VANTA_NVX_GSP_FWSEC_MMIO_H

#include "nvx_gsp_fwsec_result.h"
#include "nvx_mmio.h"

/* GA102/TU102 public BAR0 register offsets used by the FWSEC result path. */
#define NVX_GSP_FWSEC_VBIOS_SCRATCH(i) (0x00001400U + ((uint32_t)(i) * 4U))
#define NVX_GSP_FWSEC_SCRATCH_FRTS_INDEX 0x0EU
#define NVX_GSP_FWSEC_SCRATCH_SB_INDEX 0x15U
#define NVX_GSP_FWSEC_WPR2_ADDR_LO 0x001FA824U
#define NVX_GSP_FWSEC_WPR2_ADDR_HI 0x001FA828U
#define NVX_GSP_FWSEC_GFW_PLM 0x00118128U
#define NVX_GSP_FWSEC_GFW_PROGRESS 0x00118234U
#define NVX_GSP_FWSEC_GFW_PROGRESS_COMPLETED 0xFFU
#define NVX_GSP_FWSEC_PLM_READ_LEVEL0_ENABLE 0x1U
#define NVX_GSP_FWSEC_MMIO_REQUIRED_BAR_BYTES (NVX_GSP_FWSEC_WPR2_ADDR_HI + 4U)

typedef enum nvx_gsp_fwsec_mmio_status {
    NVX_GSP_FWSEC_MMIO_OK = 0,
    NVX_GSP_FWSEC_MMIO_INVALID_ARGUMENT,
    NVX_GSP_FWSEC_MMIO_BAD_OFFSET,
    NVX_GSP_FWSEC_MMIO_READ_FAILED,
    NVX_GSP_FWSEC_MMIO_OUTPUT_OVERLAP
} nvx_gsp_fwsec_mmio_status_t;

/*
 * Read the GA10x/TU102 FWSEC result registers into a decoded snapshot suitable
 * for nvx_gsp_fwsec_validate_result(). This reads status only; it does not
 * execute FWSEC or imply the command succeeded.
 */
nvx_gsp_fwsec_mmio_status_t nvx_gsp_fwsec_read_result_snapshot(
    const nvx_mmio_region_t *bar0, uint64_t requested_frts_offset,
    nvx_gsp_fwsec_result_snapshot_t *out_snapshot);

#endif
