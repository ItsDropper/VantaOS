/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_wpr_meta.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
static nvx_gsp_wpr_layout_t valid_layout(void)
{
 return (nvx_gsp_wpr_layout_t){
  .sysmem_radix3_addr=0x100000U,.radix3_size=0x200000U,
  .sysmem_bootloader_addr=0x400000U,.bootloader_size=0x10000U,
  .bootloader_code_offset=0x1000U,.bootloader_data_offset=0x8000U,.bootloader_manifest_offset=0xA000U,
  .sysmem_signature_addr=0x500000U,.signature_size=0x1000U,
  .reserved_start=0x38020000U,.nonwpr_heap_offset=0x38020000U,.nonwpr_heap_size=0x1000000U,
  .wpr_start=0x30000000U,.wpr_heap_offset=0x30001000U,.wpr_heap_size=0x100000U,
  .firmware_offset=0x30101000U,.boot_bin_offset=0x30301000U,
  .frts_offset=0x30311000U,.frts_size=0x10000U,.wpr_end=0x38000000U,
  .framebuffer_size=0x100000000ULL,.vga_workspace_offset=0x3F000000U,.vga_workspace_size=0x1000000U,
  .boot_count=7U,.crash_report_queue_addr=0x600000U,.crash_report_queue_size=0x10000U,
  .vf_partition_count=1U,.flags=NVX_GSP_FW_FLAG_CLOCK_BOOST|NVX_GSP_FW_FLAG_PPCIE_ENABLED,
  .paging_config=0x1234U
 };
}
int main(void)
{
 nvx_gsp_wpr_layout_t l=valid_layout();nvx_gsp_wpr_meta_v1_t m={0};
 assert(sizeof(m)==256U&&offsetof(nvx_gsp_wpr_meta_v1_t,verified)==248U);
 assert(nvx_gsp_wpr_meta_build(&l,&m)==NVX_GSP_WPR_OK);
 assert(m.magic==NVX_GSP_WPR_META_V1_MAGIC&&m.revision==1U&&m.verified==0U);
 assert(m.sysmem_addr_of_radix3_elf==l.sysmem_radix3_addr&&m.gsp_fw_wpr_start==l.wpr_start);
 assert(m.gsp_fw_wpr_end==l.wpr_end&&m.size_of_crash_report_queue==l.crash_report_queue_size);
 assert(nvx_gsp_wpr_meta_validate(&m)==NVX_GSP_WPR_OK);
 m.verified=NVX_GSP_WPR_META_V1_VERIFIED;
 assert(nvx_gsp_wpr_meta_validate(&m)==NVX_GSP_WPR_OK);
 m.magic=0U;assert(nvx_gsp_wpr_meta_validate(&m)==NVX_GSP_WPR_BAD_MAGIC);
 m.magic=NVX_GSP_WPR_META_V1_MAGIC;m.revision=2U;
 assert(nvx_gsp_wpr_meta_validate(&m)==NVX_GSP_WPR_BAD_REVISION);
 m.revision=1U;m.reserved1[3]=1U;
 assert(nvx_gsp_wpr_meta_validate(&m)==NVX_GSP_WPR_BAD_RESERVED);
 m.reserved1[3]=0U;m.reserved3=1U;
 assert(nvx_gsp_wpr_meta_validate(&m)==NVX_GSP_WPR_BAD_RESERVED);
 m.reserved3=0U;m.flags=0x80U;
 assert(nvx_gsp_wpr_meta_validate(&m)==NVX_GSP_WPR_BAD_FLAGS);
 m.flags=0U;m.gsp_fw_wpr_start+=1U;
 assert(nvx_gsp_wpr_meta_validate(&m)==NVX_GSP_WPR_BAD_ALIGNMENT);
 l=valid_layout();l.wpr_heap_offset=l.firmware_offset;
 assert(nvx_gsp_wpr_meta_build(&l,&m)==NVX_GSP_WPR_OVERLAP);
 l=valid_layout();l.wpr_end=0x40000000U;
 assert(nvx_gsp_wpr_meta_build(&l,&m)==NVX_GSP_WPR_OVERLAP);
 l=valid_layout();l.sysmem_signature_addr=UINT64_MAX-8U;
 assert(nvx_gsp_wpr_meta_build(&l,&m)==NVX_GSP_WPR_BAD_RANGE);
 l=valid_layout();l.flags=0x80U;
 assert(nvx_gsp_wpr_meta_build(&l,&m)==NVX_GSP_WPR_BAD_FLAGS);
 l=valid_layout();l.sysmem_signature_addr=l.sysmem_radix3_addr+0x1000U;
 assert(nvx_gsp_wpr_meta_build(&l,&m)==NVX_GSP_WPR_OVERLAP);
 l=valid_layout();
 assert(nvx_gsp_wpr_meta_build(&l,(nvx_gsp_wpr_meta_v1_t *)(void *)&l)==NVX_GSP_WPR_INVALID_ARGUMENT);
 l=valid_layout();l.bootloader_manifest_offset=l.bootloader_size;
 assert(nvx_gsp_wpr_meta_build(&l,&m)==NVX_GSP_WPR_BAD_RANGE);
 l=valid_layout();l.framebuffer_size+=1U;
 assert(nvx_gsp_wpr_meta_build(&l,&m)==NVX_GSP_WPR_BAD_ALIGNMENT);
 assert(nvx_gsp_wpr_meta_build(0,&m)==NVX_GSP_WPR_INVALID_ARGUMENT);
 assert(nvx_gsp_wpr_meta_build(&l,0)==NVX_GSP_WPR_INVALID_ARGUMENT);
 return 0;
}
