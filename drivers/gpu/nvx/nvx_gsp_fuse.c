/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_ga10x.h"

nvx_gsp_ga10x_status_t nvx_gsp_ga10x_read_ucode_fuse_version(
    const nvx_mmio_region_t *bar0, uint32_t engine_id_mask,
    uint32_t ucode_id, uint32_t *out_version)
{
    uint32_t base;
    uint32_t fuse_bitmap = 0U;
    nvx_mmio_status_t mmio_status;

    if (!bar0 || !bar0->base || !out_version)
        return NVX_GSP_GA10X_INVALID_ARGUMENT;
    *out_version = 0U;

    /* The mask is a firmware descriptor field; reject ambiguous combinations. */
    switch (engine_id_mask) {
    case NVX_GSP_GA10X_ENGINE_MASK_SEC2:
        base = NVX_GSP_GA10X_FUSE_SEC2_UCODE1_VERSION;
        break;
    case NVX_GSP_GA10X_ENGINE_MASK_NVDEC:
        base = NVX_GSP_GA10X_FUSE_NVDEC_UCODE1_VERSION;
        break;
    case NVX_GSP_GA10X_ENGINE_MASK_GSP:
        base = NVX_GSP_GA10X_FUSE_GSP_UCODE1_VERSION;
        break;
    default:
        return NVX_GSP_GA10X_FUSE_UNSUPPORTED_ENGINE;
    }

    /* Firmware descriptors number these 1..16; validate before arithmetic. */
    if (ucode_id == 0U || ucode_id > NVX_GSP_GA10X_FUSE_UCODE_COUNT)
        return NVX_GSP_GA10X_FUSE_INVALID_UCODE_ID;

    mmio_status = nvx_mmio_read32(
        bar0, (size_t)base + ((size_t)(ucode_id - 1U) * sizeof(uint32_t)),
        &fuse_bitmap);
    if (mmio_status == NVX_MMIO_OUT_OF_RANGE)
        return NVX_GSP_GA10X_BAD_BAR;
    if (mmio_status != NVX_MMIO_OK)
        return NVX_GSP_GA10X_MMIO_ERROR;

    /* Published GA102 definitions expose only bits 15:0 as fuse data. */
    if ((fuse_bitmap & 0xFFFF0000U) != 0U)
        return NVX_GSP_GA10X_FUSE_RESERVED_BITS;

    /* NVIDIA's GA102 path uses fls(bitmap): the version is bit length, not value. */
    while (fuse_bitmap != 0U) {
        ++(*out_version);
        fuse_bitmap >>= 1U;
    }
    return NVX_GSP_GA10X_OK;
}

