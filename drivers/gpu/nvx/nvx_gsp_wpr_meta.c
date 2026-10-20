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
static int overlaps_ptr(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!an||!bn)return 0;
    if(x>UINTPTR_MAX-an||y>UINTPTR_MAX-bn)return 1;
    return x<y+bn&&y<x+an;
}
static int add_ok(uint64_t a,uint64_t b,uint64_t *end)
{
    if(a>UINT64_MAX-b)return 0;
    *end=a+b;
    return 1;
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
    uint64_t nonwpr_end,vga_end,heap_end,fw_end,boot_end,frts_end;
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
       !range_ok(l->sysmem_signature_addr,l->signature_size,UINT64_MAX)||
       (l->crash_report_queue_size&&
        (!l->crash_report_queue_addr||!aligned(l->crash_report_queue_addr,16U)||
         !range_ok(l->crash_report_queue_addr,l->crash_report_queue_size,UINT64_MAX))))
        return NVX_GSP_WPR_BAD_RANGE;
    if(!l->radix3_size||!l->bootloader_size||!l->signature_size||
       !l->wpr_heap_size||!l->nonwpr_heap_size)
        return NVX_GSP_WPR_BAD_RANGE;
    if(overlap(l->sysmem_radix3_addr,l->radix3_size,l->sysmem_bootloader_addr,l->bootloader_size)||
       overlap(l->sysmem_radix3_addr,l->radix3_size,l->sysmem_signature_addr,l->signature_size)||
       overlap(l->sysmem_bootloader_addr,l->bootloader_size,l->sysmem_signature_addr,l->signature_size)||
       overlap(l->crash_report_queue_addr,l->crash_report_queue_size,l->sysmem_radix3_addr,l->radix3_size)||
       overlap(l->crash_report_queue_addr,l->crash_report_queue_size,l->sysmem_bootloader_addr,l->bootloader_size)||
       overlap(l->crash_report_queue_addr,l->crash_report_queue_size,l->sysmem_signature_addr,l->signature_size))
        return NVX_GSP_WPR_OVERLAP;
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
    if(!add_ok(l->nonwpr_heap_offset,l->nonwpr_heap_size,&nonwpr_end)||
       !add_ok(l->vga_workspace_offset,l->vga_workspace_size,&vga_end)||
       !add_ok(l->wpr_heap_offset,l->wpr_heap_size,&heap_end)||
       !add_ok(l->firmware_offset,l->radix3_size,&fw_end)||
       !add_ok(l->boot_bin_offset,l->bootloader_size,&boot_end)||
       !add_ok(l->frts_offset,l->frts_size,&frts_end))
        return NVX_GSP_WPR_BAD_RANGE;
    if(heap_end>l->wpr_end||fw_end>l->wpr_end||boot_end>l->wpr_end||
       (l->frts_size&&(frts_end>l->wpr_end||l->frts_offset<l->wpr_start))||
       l->wpr_end<l->wpr_start+NVX_GSP_WPR_META_V1_SIZE)
        return NVX_GSP_WPR_BAD_RANGE;
    if(overlap(l->wpr_heap_offset,l->wpr_heap_size,l->firmware_offset,l->radix3_size)||
       overlap(l->wpr_heap_offset,l->wpr_heap_size,l->boot_bin_offset,l->bootloader_size)||
       overlap(l->firmware_offset,l->radix3_size,l->boot_bin_offset,l->bootloader_size)||
       overlap(l->frts_offset,l->frts_size,l->wpr_heap_offset,l->wpr_heap_size)||
       overlap(l->frts_offset,l->frts_size,l->firmware_offset,l->radix3_size)||
       overlap(l->frts_offset,l->frts_size,l->boot_bin_offset,l->bootloader_size)||
       overlap(l->nonwpr_heap_offset,l->nonwpr_heap_size,l->wpr_start,l->wpr_end-l->wpr_start)||
       overlap(l->vga_workspace_offset,l->vga_workspace_size,l->nonwpr_heap_offset,l->nonwpr_heap_size)||
       overlap(l->vga_workspace_offset,l->vga_workspace_size,l->wpr_start,l->wpr_end-l->wpr_start))
        return NVX_GSP_WPR_OVERLAP;
    (void)nonwpr_end;(void)vga_end;
    return NVX_GSP_WPR_OK;
}
nvx_gsp_wpr_status_t nvx_gsp_wpr_meta_build(const nvx_gsp_wpr_layout_t *l,
    nvx_gsp_wpr_meta_v1_t *out)
{
    nvx_gsp_wpr_status_t s;
    if(!l||!out)return NVX_GSP_WPR_INVALID_ARGUMENT;
    if(overlaps_ptr(l,sizeof(*l),out,sizeof(*out)))return NVX_GSP_WPR_INVALID_ARGUMENT;
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
    out->paging_config=l->paging_config;out->reserved3=0U;
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
    l.paging_config=m->paging_config;
    if(m->verified!=0U&&m->verified!=NVX_GSP_WPR_META_V1_VERIFIED)return NVX_GSP_WPR_BAD_RESERVED;
    return validate_layout(&l);
}

static int align_up_u64(uint64_t value, uint64_t alignment, uint64_t *out)
{
    uint64_t rem;
    if (!out || !alignment)
        return 0;
    rem = value % alignment;
    if (!rem) { *out = value; return 1; }
    return add_ok(value, alignment - rem, out);
}
static uint64_t align_down_u64(uint64_t value, uint64_t alignment)
{
    return value - value % alignment;
}

nvx_gsp_wpr_status_t nvx_gsp_wpr_layout_plan(
    const nvx_gsp_wpr_layout_t *resources,
    const nvx_gsp_wpr_plan_config_t *config,
    nvx_gsp_wpr_layout_t *out_layout)
{
    nvx_gsp_wpr_layout_t plan;
    uint64_t vga_end, reserved_end, meta_size, nonwpr_size;
    uint64_t pre_heap, post_heap, fw_heap_offset, actual_fw_heap;
    uint64_t fw_offset, boot_offset, frts_offset, wpr_end, wpr_start;
    uint64_t nonwpr_offset, requested_wpr_heap_size;
    const uint64_t mb = UINT64_C(1024) * 1024U;

    if (!resources || !config || !out_layout ||
        overlaps_ptr(resources, sizeof(*resources), out_layout, sizeof(*out_layout)) ||
        overlaps_ptr(config, sizeof(*config), out_layout, sizeof(*out_layout)) ||
        overlaps_ptr(resources, sizeof(*resources), config, sizeof(*config)))
        return NVX_GSP_WPR_INVALID_ARGUMENT;
    if (!config->framebuffer_size ||
        !aligned(config->framebuffer_size, NVX_GSP_FB_ALIGN) ||
        config->vga_workspace_offset >= config->framebuffer_size ||
        !range_ok(config->vga_workspace_offset,
                  config->framebuffer_size - config->vga_workspace_offset,
                  config->framebuffer_size) ||
        (config->mmu_lock_valid && config->mmu_lock_low >= config->framebuffer_size) ||
        !resources->radix3_size || !resources->bootloader_size ||
        !resources->signature_size || !config->compute_wpr_heap_size ||
        !config->nonwpr_heap_size ||
        config->frts_size > config->framebuffer_size)
        return NVX_GSP_WPR_BAD_RANGE;

    vga_end = config->framebuffer_size;
    reserved_end = config->vga_workspace_offset;
    if (config->mmu_lock_valid && config->mmu_lock_low < reserved_end)
        reserved_end = config->mmu_lock_low;
    if (config->wpr_end_margin > reserved_end)
        return NVX_GSP_WPR_BAD_RANGE;
    wpr_end = align_down_u64(reserved_end - config->wpr_end_margin,
                             NVX_GSP_WPR_ALIGN);
    if (config->frts_size > wpr_end)
        return NVX_GSP_WPR_BAD_RANGE;
    frts_offset = wpr_end - config->frts_size;
    if (resources->bootloader_size > frts_offset)
        return NVX_GSP_WPR_BAD_RANGE;
    boot_offset = align_down_u64(frts_offset - resources->bootloader_size,
                                 4096U);
    if (resources->radix3_size > boot_offset)
        return NVX_GSP_WPR_BAD_RANGE;
    fw_offset = align_down_u64(boot_offset - resources->radix3_size, 65536U);

    if (!align_up_u64(config->nonwpr_heap_size, mb, &nonwpr_size) ||
        !align_up_u64(NVX_GSP_WPR_META_V1_SIZE, mb, &meta_size) ||
        !add_ok(meta_size, nonwpr_size, &pre_heap) ||
        !align_up_u64(config->framebuffer_size - fw_offset, mb, &post_heap))
        return NVX_GSP_WPR_BAD_RANGE;
    if (!config->compute_wpr_heap_size(config->heap_policy_context, pre_heap,
                                       post_heap, &requested_wpr_heap_size) ||
        requested_wpr_heap_size == 0U || requested_wpr_heap_size > fw_offset)
        return NVX_GSP_WPR_BAD_RANGE;
    fw_heap_offset = align_down_u64(fw_offset - requested_wpr_heap_size, mb);
    actual_fw_heap = align_down_u64(fw_offset - fw_heap_offset, mb);
    if (!actual_fw_heap || fw_heap_offset < meta_size)
        return NVX_GSP_WPR_BAD_RANGE;
    wpr_start = fw_heap_offset - meta_size;
    if (nonwpr_size > wpr_start)
        return NVX_GSP_WPR_BAD_RANGE;
    nonwpr_offset = wpr_start - nonwpr_size;

    plan = *resources;
    plan.framebuffer_size = config->framebuffer_size;
    plan.vga_workspace_offset = config->vga_workspace_offset;
    plan.vga_workspace_size = vga_end - config->vga_workspace_offset;
    plan.wpr_end = wpr_end;
    plan.frts_offset = frts_offset;
    plan.frts_size = config->frts_size;
    plan.boot_bin_offset = boot_offset;
    plan.firmware_offset = fw_offset;
    plan.wpr_start = wpr_start;
    plan.wpr_heap_offset = fw_heap_offset;
    plan.wpr_heap_size = actual_fw_heap;
    plan.nonwpr_heap_offset = nonwpr_offset;
    plan.nonwpr_heap_size = nonwpr_size;
    plan.reserved_start = nonwpr_offset;

    /* Validate every range/overlap before exposing a partially planned layout. */
    nvx_gsp_wpr_status_t status = validate_layout(&plan);
    if (status != NVX_GSP_WPR_OK)
        return status;
    *out_layout = plan;
    return NVX_GSP_WPR_OK;
}
