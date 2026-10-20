/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_wpr_meta.h"
#include <string.h>
_Static_assert(sizeof(nvx_gsp_wpr_meta_v1_t)==NVX_GSP_WPR_META_V1_SIZE,"WPR V1 must remain 256 bytes");
_Static_assert(offsetof(nvx_gsp_wpr_meta_v1_t,sysmem_addr_of_radix3_elf)==16U,"WPR V1 ELF address offset");
_Static_assert(offsetof(nvx_gsp_wpr_meta_v1_t,gsp_fw_wpr_start)==112U,"WPR V1 WPR start offset");
_Static_assert(offsetof(nvx_gsp_wpr_meta_v1_t,verified)==248U,"WPR V1 verified offset");
static int add_ok(uint64_t a,uint64_t b,uint64_t *end)
{
    if(a>UINT64_MAX-b)return 0;*end=a+b;return 1;
}
static int range_ok(uint64_t start,uint64_t size,uint64_t limit)
{
    uint64_t end;return add_ok(start,size,&end)&&end<=limit;
}
static int aligned(uint64_t value,uint64_t alignment)
{
    return (value&(alignment-1U))==0U;
}
static int overlap(uint64_t a,uint64_t as,uint64_t b,uint64_t bs)
{
    uint64_t ae,be;if(!as||!bs)return 0;
    if(!add_ok(a,as,&ae)||!add_ok(b,bs,&be))return 1;
    return a<be&&b<ae;
}
static nvx_gsp_wpr_status_t validate_layout(const nvx_gsp_wpr_layout_t *l)
{
    uint64_t wpr_end,nonwpr_end,vga_end,heap_end,fw_end,boot_end,frts_end;
    if(!l)return NVX_GSP_WPR_INVALID_ARGUMENT;
    if((l->flags&~NVX_GSP_FW_FLAGS_VALID)!=0U)return NVX_GSP_WPR_BAD_FLAGS;
    if(!l->framebuffer_size||!aligned(l->framebuffer_size,NVX_GSP_FB_ALIGN)||
       !aligned(l->wpr_start,NVX_GSP_WPR_ALIGN)||
       !aligned(l->wpr_end,NVX_GSP_WPR_ALIGN))
        return NVX_GSP_WPR_BAD_ALIGNMENT;
    if(l->wpr_start>=l->wpr_end||l->wpr_end>l->framebuffer_size||
       !range_ok(l->nonwpr_heap_offset,l->nonwpr_heap_size,l->framebuffer_size)||
       !range_ok(l->vga_workspace_offset,l->vga_workspace_size,l->framebuffer_size)||
       !range_ok(l->wpr_heap_offset,l->wpr_heap_size,l->framebuffer_size)||
       !range_ok(l->firmware_offset,l->radix3_size,l->framebuffer_size)||
       !range_ok(l->boot_bin_offset,l->bootloader_size,l->framebuffer_size)||
       !range_ok(l->frts_offset,l->frts_size,l->framebuffer_size)||
       !range_ok(l->sysmem_radix3_addr,l->radix3_size,UINT64_MAX)||
       !range_ok(l->sysmem_bootloader_addr,l->bootloader_size,UINT64_MAX)||
       !range_ok(l->sysmem_signature_addr,l->signature_size,UINT64_MAX))
        return NVX_GSP_WPR_BAD_RANGE;
    if(!l->radix3_size||!l->bootloader_size||!l->signature_size||
       !l->wpr_heap_size||!l->nonwpr_heap_size)
        return NVX_GSP_WPR_BAD_RANGE;
    if(!aligned(l->sysmem_radix3_addr,16U)||!aligned(l->sysmem_bootloader_addr,16U)||
       !aligned(l->sysmem_signature_addr,16U))
        return NVX_GSP_WPR_BAD_ALIGNMENT;
    if(l->bootloader_code_offset>=l->bootloader_size||
       l->bootloader_data_offset>=l->bootloader_size||
       l->bootloader_manifest_offset>=l->bootloader_size)
        return NVX_GSP_WPR_BAD_RANGE;
    if(l->reserved_start!=l->nonwpr_heap_offset||
       l->wpr_heap_offset<l->wpr_start+NVX_GSP_WPR_META_V1_SIZE||
       l->firmware_offset<l->wpr_heap_offset||
       l->boot_bin_offset<l->firmware_offset||
       l->frts_offset<l->boot_bin_offset)
        return NVX_GSP_WPR_BAD_RANGE;
    if(!add_ok(l->wpr_start,0U,&wpr_end)||
       !add_ok(l->nonwpr_heap_offset,l->nonwpr_heap_size,&nonwpr_end)||
       !add_ok(l->vga_workspace_offset,l->vga_workspace_size,&vga_end)||
       !add_ok(l->wpr_heap_offset,l->wpr_heap_size,&heap_end)||
       !add_ok(l->firmware_offset,l->radix3_size,&fw_end)||
       !add_ok(l->boot_bin_offset,l->bootloader_size,&boot_end)||
       !add_ok(l->frts_offset,l->frts_size,&frts_end))
        return NVX_GSP_WPR_BAD_RANGE;
    if(heap_end>l->wpr_end||fw_end>l->wpr_end||boot_end>l->wpr_end||
       (l->frts_size&&frts_end>l->framebuffer_size)||
       l->wpr_end<l->wpr_start+NVX_GSP_WPR_META_V1_SIZE)
        return NVX_GSP_WPR_BAD_RANGE;
    if(overlap(l->wpr_heap_offset,l->wpr_heap_size,l->firmware_offset,l->radix3_size)||
       overlap(l->wpr_heap_offset,l->wpr_heap_size,l->boot_bin_offset,l->bootloader_size)||
       overlap(l->firmware_offset,l->radix3_size,l->boot_bin_offset,l->bootloader_size)||
       overlap(l->nonwpr_heap_offset,l->nonwpr_heap_size,l->wpr_start,l->wpr_end-l->wpr_start)||
       overlap(l->vga_workspace_offset,l->vga_workspace_size,l->nonwpr_heap_offset,l->nonwpr_heap_size)||
       overlap(l->vga_workspace_offset,l->vga_workspace_size,l->wpr_start,l->wpr_end-l->wpr_start))
        return NVX_GSP_WPR_OVERLAP;
    (void)wpr_end;(void)nonwpr_end;(void)vga_end;
    return NVX_GSP_WPR_OK;
}
nvx_gsp_wpr_status_t nvx_gsp_wpr_meta_build(const nvx_gsp_wpr_layout_t *l,
    nvx_gsp_wpr_meta_v1_t *out)
{
    nvx_gsp_wpr_status_t s;
    if(!l||!out)return NVX_GSP_WPR_INVALID_ARGUMENT;
    s=validate_layout(l);if(s!=NVX_GSP_WPR_OK)return s;
    memset(out,0,sizeof(*out));
    out->magic=NVX_GSP_WPR_META_V1_MAGIC;out->revision=NVX_GSP_WPR_META_V1_REVISION;
    out->sysmem_addr_of_radix3_elf=l->sysmem_radix3_addr;out->size_of_radix3_elf=l->radix3_size;
    out->sysmem_addr_of_bootloader=l->sysmem_bootloader_addr;out->size_of_bootloader=l->bootloader_size;
    out->bootloader_code_offset=l->bootloader_code_offset;out->bootloader_data_offset=l->bootloader_data_offset;
    out->bootloader_manifest_offset=l->bootloader_manifest_offset;
    out->sysmem_addr_of_signature=l->sysmem_signature_addr;out->size_of_signature=l->signature_size;
    out->gsp_fw_reserved_start=l->reserved_start;
    out->nonwpr_heap_offset=l->nonwpr_heap_offset;out->nonwpr_heap_size=l->nonwpr_heap_size;
    out->gsp_fw_wpr_start=l->wpr_start;out->gsp_fw_heap_offset=l->wpr_heap_offset;
    out->gsp_fw_heap_size=l->wpr_heap_size;out->gsp_fw_offset=l->firmware_offset;
    out->boot_bin_offset=l->boot_bin_offset;out->frts_offset=l->frts_offset;out->frts_size=l->frts_size;
    out->gsp_fw_wpr_end=l->wpr_end;out->fb_size=l->framebuffer_size;
    out->vga_workspace_offset=l->vga_workspace_offset;out->vga_workspace_size=l->vga_workspace_size;
    out->boot_count=l->boot_count;out->sysmem_addr_of_crash_report_queue=l->crash_report_queue_addr;
    out->size_of_crash_report_queue=l->crash_report_queue_size;
    out->gsp_fw_heap_vf_partition_count=l->vf_partition_count;out->flags=l->flags;
    out->paging_config=l->paging_config;out->reserved3=l->pmu_reserved_size;
    out->verified=0U;
    return NVX_GSP_WPR_OK;
}
nvx_gsp_wpr_status_t nvx_gsp_wpr_meta_validate(const nvx_gsp_wpr_meta_v1_t *m)
{
    nvx_gsp_wpr_layout_t l;
    if(!m)return NVX_GSP_WPR_INVALID_ARGUMENT;
    if(m->magic!=NVX_GSP_WPR_META_V1_MAGIC)return NVX_GSP_WPR_BAD_MAGIC;
    if(m->revision!=NVX_GSP_WPR_META_V1_REVISION)return NVX_GSP_WPR_BAD_REVISION;
    for(size_t i=0;i<sizeof(m->reserved1);i++)if(m->reserved1[i])return NVX_GSP_WPR_BAD_RESERVED;
    if(m->reserved2||m->reserved3)return NVX_GSP_WPR_BAD_RESERVED;
    memset(&l,0,sizeof(l));
    l.sysmem_radix3_addr=m->sysmem_addr_of_radix3_elf;l.radix3_size=m->size_of_radix3_elf;
    l.sysmem_bootloader_addr=m->sysmem_addr_of_bootloader;l.bootloader_size=m->size_of_bootloader;
    l.bootloader_code_offset=m->bootloader_code_offset;l.bootloader_data_offset=m->bootloader_data_offset;
    l.bootloader_manifest_offset=m->bootloader_manifest_offset;
    l.sysmem_signature_addr=m->sysmem_addr_of_signature;l.signature_size=m->size_of_signature;
    l.reserved_start=m->gsp_fw_reserved_start;l.nonwpr_heap_offset=m->nonwpr_heap_offset;
    l.nonwpr_heap_size=m->nonwpr_heap_size;l.wpr_start=m->gsp_fw_wpr_start;
    l.wpr_heap_offset=m->gsp_fw_heap_offset;l.wpr_heap_size=m->gsp_fw_heap_size;
    l.firmware_offset=m->gsp_fw_offset;l.boot_bin_offset=m->boot_bin_offset;
    l.frts_offset=m->frts_offset;l.frts_size=m->frts_size;l.wpr_end=m->gsp_fw_wpr_end;
    l.framebuffer_size=m->fb_size;l.vga_workspace_offset=m->vga_workspace_offset;
    l.vga_workspace_size=m->vga_workspace_size;l.boot_count=m->boot_count;
    l.crash_report_queue_addr=m->sysmem_addr_of_crash_report_queue;
    l.crash_report_queue_size=m->size_of_crash_report_queue;
    l.vf_partition_count=m->gsp_fw_heap_vf_partition_count;l.flags=m->flags;
    l.paging_config=m->paging_config;l.pmu_reserved_size=m->reserved3;
    if(m->verified!=0U&&m->verified!=NVX_GSP_WPR_META_V1_VERIFIED)return NVX_GSP_WPR_BAD_RESERVED;
    return validate_layout(&l);
}
