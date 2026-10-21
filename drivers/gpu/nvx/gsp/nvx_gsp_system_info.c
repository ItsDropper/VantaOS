/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_system_info.h"

#include <stdint.h>
#include <string.h>

static int overlaps(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x=(uintptr_t)a, y=(uintptr_t)b;
    if (!an || !bn) return 0;
    if (!a || !b || x>UINTPTR_MAX-an || y>UINTPTR_MAX-bn) return 1;
    return x<y+bn && y<x+an;
}

#define TRY(call) do { status=(call); if(status!=NVX_GSP_NVKV_OK) return status; } while(0)

nvx_gsp_nvkv_status_t nvx_gsp_system_info_encode(
    const nvx_gsp_system_info_t *info, void *buffer, size_t capacity,
    size_t *out_size_bytes)
{
    nvx_gsp_nvkv_writer_t writer={0};
    nvx_gsp_nvkv_status_t status;
    uint32_t pci_ids[3];
    uint32_t config_mirror[2];
    uint32_t host_identity[2];
    uint64_t flags[3];
    uint64_t gpu_addresses[4];
    uint64_t domain_surfaces[3];
    uint64_t atomics_console_va[3];
    uint64_t businfo[2];

    if (out_size_bytes) *out_size_bytes=0U;
    if (!info || !buffer || !capacity || !out_size_bytes || !info->present ||
        overlaps(info,sizeof(*info),buffer,capacity) ||
        overlaps(out_size_bytes,sizeof(*out_size_bytes),buffer,capacity) ||
        overlaps(out_size_bytes,sizeof(*out_size_bytes),info,sizeof(*info)))
        return NVX_GSP_NVKV_INVALID_ARGUMENT;

    TRY(nvx_gsp_nvkv_begin(&writer,buffer,capacity));

    if (info->present & NVX_GSP_SI_PCI_IDENTITY) {
        pci_ids[0]=info->pci_device_id;
        pci_ids[1]=info->pci_sub_device_id;
        pci_ids[2]=info->pci_revision_id;
        TRY(nvx_gsp_nvkv_put_seq32(&writer,0U,0x0001U,pci_ids,3U));
    }
    if (info->present & NVX_GSP_SI_PCI_CONFIG_MIRROR) {
        config_mirror[0]=info->pci_config_mirror_base;
        config_mirror[1]=info->pci_config_mirror_size;
        TRY(nvx_gsp_nvkv_put_seq32(&writer,0U,0x0010U,config_mirror,2U));
    }
    if (info->present & NVX_GSP_SI_HOST_IDENTITY) {
        host_identity[0]=info->host_arch;
        host_identity[1]=info->host_os_type;
        TRY(nvx_gsp_nvkv_put_seq32(&writer,0U,0x0070U,host_identity,2U));
    }
    if (info->present & NVX_GSP_SI_HOST_PAGE_SIZE)
        TRY(nvx_gsp_nvkv_put_imm32(&writer,0U,0x0078U,info->host_page_size));

    if (info->present & NVX_GSP_SI_PLATFORM_FLAGS) {
        flags[0]=info->pcie_flags;
        flags[1]=info->platform_flags;
        flags[2]=info->feature_flags;
        TRY(nvx_gsp_nvkv_put_seq64(&writer,0U,0x1000U,flags,3U));
    }
    if (info->present & NVX_GSP_SI_GPU_PHYS_ADDRESSES) {
        gpu_addresses[0]=info->gpu_phys_addr;
        gpu_addresses[1]=info->gpu_phys_fb_addr;
        gpu_addresses[2]=info->gpu_phys_inst_addr;
        gpu_addresses[3]=info->gpu_phys_io_addr;
        TRY(nvx_gsp_nvkv_put_seq64(&writer,0U,0x1010U,gpu_addresses,4U));
    }
    if (info->present & NVX_GSP_SI_DOMAIN_SURFACES) {
        domain_surfaces[0]=info->nv_domain_bus_device_func;
        domain_surfaces[1]=info->sim_access_buf_phys_addr;
        domain_surfaces[2]=info->notify_op_shared_surface_phys_addr;
        TRY(nvx_gsp_nvkv_put_seq64(&writer,0U,0x1020U,domain_surfaces,3U));
    }
    if (info->present & NVX_GSP_SI_ATOMICS_CONSOLE_VA) {
        atomics_console_va[0]=info->pcie_atomics_op_mask;
        atomics_console_va[1]=info->console_mem_size;
        atomics_console_va[2]=info->max_user_va;
        TRY(nvx_gsp_nvkv_put_seq64(&writer,0U,0x1030U,atomics_console_va,3U));
    }
    if (info->present & NVX_GSP_SI_TIMER_OFFSET) {
        TRY(nvx_gsp_nvkv_put_seq64(&writer,0U,0x1040U,
                                   &info->sys_timer_offset_ns,1U));
    }
    if (info->present & NVX_GSP_SI_FHB_BUSINFO) {
        businfo[0]=info->fhb_businfo;
        businfo[1]=info->chipset_businfo;
        TRY(nvx_gsp_nvkv_put_seq64(&writer,0U,0x1090U,businfo,2U));
    }

    *out_size_bytes=nvx_gsp_nvkv_size_bytes(&writer);
    if (*out_size_bytes==0U ||
        nvx_gsp_nvkv_validate(buffer,*out_size_bytes)!=NVX_GSP_NVKV_OK) {
        *out_size_bytes=0U;
        return NVX_GSP_NVKV_BAD_STREAM;
    }
    return NVX_GSP_NVKV_OK;
}

#undef TRY
