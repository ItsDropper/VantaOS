/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_NVX_GSP_STATIC_INFO_H
#define VANTA_NVX_GSP_STATIC_INFO_H

#include <stddef.h>
#include <stdint.h>
#include "nvx_gsp_nvkv.h"

#define NVX_GSP_STATIC_MAX_FB_REGIONS 16U
#define NVX_GSP_STATIC_GPU_NAME_BYTES 64U
#define NVX_GSP_STATIC_SHORT_NAME_BYTES 32U

#define NVX_GSP_SC_BOARD_ID              (UINT64_C(1)<<0)
#define NVX_GSP_SC_SKU_CONFIG_VERSION    (UINT64_C(1)<<1)
#define NVX_GSP_SC_SKU_BUSINESS_CYCLE    (UINT64_C(1)<<2)
#define NVX_GSP_SC_CHIP_SUB_REV          (UINT64_C(1)<<3)
#define NVX_GSP_SC_FB_REGION_COUNT       (UINT64_C(1)<<4)
#define NVX_GSP_SC_FB_LENGTH             (UINT64_C(1)<<5)
#define NVX_GSP_SC_BRANDING              (UINT64_C(1)<<6)
#define NVX_GSP_SC_MISC_FEATURES         (UINT64_C(1)<<7)
#define NVX_GSP_SC_POWER_FEATURES        (UINT64_C(1)<<8)
#define NVX_GSP_SC_BAR1_PDE_BASE         (UINT64_C(1)<<9)
#define NVX_GSP_SC_BAR2_PDE_BASE         (UINT64_C(1)<<10)
#define NVX_GSP_SC_WPR_HEAP_OFFSET       (UINT64_C(1)<<11)
#define NVX_GSP_SC_WPR_FRTS_OFFSET       (UINT64_C(1)<<12)
#define NVX_GSP_SC_GPU_NAME              (UINT64_C(1)<<13)
#define NVX_GSP_SC_GPU_SHORT_NAME        (UINT64_C(1)<<14)

typedef struct nvx_gsp_static_fb_region {
    uint64_t base;
    uint64_t limit;
    uint64_t reserved;
    uint32_t flags;
    uint32_t tag;
    uint32_t performance;
    uint8_t has_base, has_limit, has_reserved;
    uint8_t has_flags, has_tag, has_performance;
} nvx_gsp_static_fb_region_t;

typedef struct nvx_gsp_static_info {
    uint64_t present;
    uint32_t board_id, sku_config_version, sku_business_cycle, chip_sub_revision;
    uint32_t fb_region_count;
    uint64_t fb_length, branding, misc_features, power_features;
    uint64_t bar1_pde_base, bar2_pde_base;
    uint64_t wpr_non_wpr_heap_offset, wpr_frts_offset;
    nvx_gsp_static_fb_region_t fb_regions[NVX_GSP_STATIC_MAX_FB_REGIONS];
    char gpu_name[NVX_GSP_STATIC_GPU_NAME_BYTES];
    char gpu_short_name[NVX_GSP_STATIC_SHORT_NAME_BYTES];
} nvx_gsp_static_info_t;

/*
 * Decode NVIDIA GMCAPI_GSP_INIT static-info NVKV using public gmcapi_gsp_config
 * key IDs. Unknown well-formed keys are skipped. Region indices outside the
 * bounded output array are rejected rather than silently truncated.
 */
nvx_gsp_nvkv_status_t nvx_gsp_static_info_decode(
    const void *buffer, size_t size_bytes, nvx_gsp_static_info_t *out_info);

#endif
