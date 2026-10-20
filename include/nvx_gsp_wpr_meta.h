/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 *
 * The 256-byte V1 wire layout follows NVIDIA's MIT-licensed
 * gsp_fw_wpr_meta_v1.h (Turing through Ada).
 */
#ifndef VANTA_NVX_GSP_WPR_META_H
#define VANTA_NVX_GSP_WPR_META_H
#include <stddef.h>
#include <stdint.h>
#define NVX_GSP_WPR_META_V1_SIZE 256U
#define NVX_GSP_WPR_META_V1_REVISION 1U
#define NVX_GSP_WPR_META_V1_MAGIC UINT64_C(0xdc3aae21371a60b3)
#define NVX_GSP_WPR_META_V1_VERIFIED UINT64_C(0xa0a0a0a0a0a0a0a0)
#define NVX_GSP_WPR_ALIGN (128U*1024U)
#define NVX_GSP_FB_ALIGN (1024U*1024U)
#define NVX_GSP_FW_FLAG_CLOCK_BOOST (1U<<0U)
#define NVX_GSP_FW_FLAG_PPCIE_ENABLED (1U<<2U)
#define NVX_GSP_FW_FLAG_MULTI_GPU_NVLE (1U<<3U)
#define NVX_GSP_FW_FLAGS_VALID (NVX_GSP_FW_FLAG_CLOCK_BOOST|NVX_GSP_FW_FLAG_PPCIE_ENABLED|NVX_GSP_FW_FLAG_MULTI_GPU_NVLE)
typedef enum nvx_gsp_wpr_status {
    NVX_GSP_WPR_OK=0,
    NVX_GSP_WPR_INVALID_ARGUMENT,
    NVX_GSP_WPR_BAD_MAGIC,
    NVX_GSP_WPR_BAD_REVISION,
    NVX_GSP_WPR_BAD_ALIGNMENT,
    NVX_GSP_WPR_BAD_RANGE,
    NVX_GSP_WPR_OVERLAP,
    NVX_GSP_WPR_BAD_FLAGS,
    NVX_GSP_WPR_BAD_RESERVED
} nvx_gsp_wpr_status_t;
/* Caller-computed physical layout. All addresses are physical, not CPU virtual. */
typedef struct nvx_gsp_wpr_layout {
    uint64_t sysmem_radix3_addr,radix3_size;
    uint64_t sysmem_bootloader_addr,bootloader_size;
    uint64_t bootloader_code_offset,bootloader_data_offset,bootloader_manifest_offset;
    uint64_t sysmem_signature_addr,signature_size;
    uint64_t reserved_start;
    uint64_t nonwpr_heap_offset,nonwpr_heap_size;
    uint64_t wpr_start;
    uint64_t wpr_heap_offset,wpr_heap_size;
    uint64_t firmware_offset;
    uint64_t boot_bin_offset;
    uint64_t frts_offset,frts_size;
    uint64_t wpr_end;
    uint64_t framebuffer_size;
    uint64_t vga_workspace_offset,vga_workspace_size;
    uint64_t boot_count;
    uint64_t crash_report_queue_addr;
    uint32_t crash_report_queue_size;
    uint8_t vf_partition_count,flags;
    uint16_t paging_config;
} nvx_gsp_wpr_layout_t;
/* Exact V1 field order; do not pack or reorder this wire structure. */
/*
 * Platform inputs are explicit: framebuffer size, VBIOS workspace, MMU lock,
 * WPR margin, FRTS size and firmware heap policy are hardware/firmware facts
 * supplied by the probed device, never guessed by NVX.
 */
typedef int (*nvx_gsp_wpr_heap_size_fn)(
    void *context, uint64_t pre_heap_size, uint64_t post_heap_size,
    uint64_t *out_heap_size);
typedef struct nvx_gsp_wpr_plan_config {
    uint64_t framebuffer_size;
    uint64_t vga_workspace_offset;
    uint64_t mmu_lock_low;
    uint64_t wpr_end_margin;
    uint64_t frts_size;
    uint64_t nonwpr_heap_size;
    nvx_gsp_wpr_heap_size_fn compute_wpr_heap_size;
    void *heap_policy_context;
    uint8_t mmu_lock_valid;
} nvx_gsp_wpr_plan_config_t;
typedef struct nvx_gsp_wpr_meta_v1 {
    uint64_t magic,revision;
    uint64_t sysmem_addr_of_radix3_elf,size_of_radix3_elf;
    uint64_t sysmem_addr_of_bootloader,size_of_bootloader;
    uint64_t bootloader_code_offset,bootloader_data_offset,bootloader_manifest_offset;
    uint64_t sysmem_addr_of_signature,size_of_signature;
    uint64_t gsp_fw_reserved_start;
    uint64_t nonwpr_heap_offset,nonwpr_heap_size;
    uint64_t gsp_fw_wpr_start;
    uint64_t gsp_fw_heap_offset,gsp_fw_heap_size;
    uint64_t gsp_fw_offset;
    uint64_t boot_bin_offset;
    uint64_t frts_offset,frts_size;
    uint64_t gsp_fw_wpr_end;
    uint64_t fb_size;
    uint64_t vga_workspace_offset,vga_workspace_size;
    uint64_t boot_count;
    uint8_t reserved1[16];
    uint64_t sysmem_addr_of_crash_report_queue;
    uint32_t size_of_crash_report_queue;
    uint32_t reserved2;
    uint8_t gsp_fw_heap_vf_partition_count,flags;
    uint16_t paging_config;
    uint32_t reserved3;
    uint64_t verified;
} nvx_gsp_wpr_meta_v1_t;
nvx_gsp_wpr_status_t nvx_gsp_wpr_layout_plan(
    const nvx_gsp_wpr_layout_t *resources,
    const nvx_gsp_wpr_plan_config_t *config,
    nvx_gsp_wpr_layout_t *out_layout);
nvx_gsp_wpr_status_t nvx_gsp_wpr_meta_build(
    const nvx_gsp_wpr_layout_t *,nvx_gsp_wpr_meta_v1_t *out);
nvx_gsp_wpr_status_t nvx_gsp_wpr_meta_validate(const nvx_gsp_wpr_meta_v1_t *);
#endif
