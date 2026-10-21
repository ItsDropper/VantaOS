/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_startup.h"
#include "nvx_device.h"

#include <stdint.h>
#include <string.h>

static int overlap(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!an||!bn)return 0;
    if(!a||!b||x>UINTPTR_MAX-an||y>UINTPTR_MAX-bn)return 1;
    return x<y+bn&&y<x+an;
}

static int dma_overlap(const void *buffer,size_t capacity,
                       const nvx_dma_buffer_t *dma)
{
    return dma&&dma->cpu_address&&dma->size&&
           overlap(buffer,capacity,dma->cpu_address,dma->size);
}

static int boot_memory_overlap(const void *buffer,size_t capacity,
                               const nvx_gsp_boot_memory_t *m)
{
    if(!m)return 0;
    return dma_overlap(buffer,capacity,&m->radix3_elf)||
           dma_overlap(buffer,capacity,&m->bootloader_payload)||
           dma_overlap(buffer,capacity,&m->signature)||
           dma_overlap(buffer,capacity,&m->booter_load)||
           dma_overlap(buffer,capacity,&m->booter_unload)||
           dma_overlap(buffer,capacity,&m->wpr_metadata);
}

nvx_gsp_startup_status_t nvx_gsp_ga10x_start_and_init_rm_from_pci(
    const nvx_gsp_startup_platform_info_t *platform,
    nvx_dma_buffer_t *staged_vbios,nvx_dma_sync_t *staged_vbios_sync,
    const nvx_gsp_vbios_fwsec_info_t *fwsec_info,uint32_t fuse_version,
    nvx_gsp_boot_memory_t *boot_memory,
    nvx_dma_buffer_t *init_args,nvx_dma_sync_t *init_args_sync,
    nvx_dma_buffer_t *shared_queue,nvx_dma_sync_t *shared_queue_sync,
    const nvx_gsp_modern_shared_queue_layout_t *queue_layout,
    const nvx_mmio_region_t *bar0,uint64_t frts_framebuffer_offset,
    void *static_info,size_t static_info_capacity,size_t *out_static_info_size,
    void *scratch,size_t scratch_size,uint32_t timeout_ms,
    uint32_t poll_interval_ms,nvx_gsp_delay_ms_fn delay_ms,void *context,
    nvx_gsp_msgq_ga10x_t *queue_runtime,nvx_gsp_gmcapi_transport_t *gmcapi,
    nvx_gsp_msgq_rpc_transport_t *rpc_transport,
    nvx_gsp_rpc_manager_t *rpc_manager,nvx_gsp_startup_result_t *out_result)
{
    nvx_gsp_system_info_t collected={0};
    size_t encoded_size=0U;
    nvx_gsp_nvkv_status_t si_status;
    nvx_generation_family_t family = NVX_FAMILY_UNKNOWN;
    nvx_device_profile_t profile = NVX_PROFILE_UNKNOWN;

    if(!platform||!platform->pci_device||!platform->trusted_gpu_facts||
       !platform->encoded_system_info_buffer||
       !platform->encoded_system_info_capacity||!staged_vbios||
       !staged_vbios_sync||!fwsec_info||!boot_memory||!init_args||
       !init_args_sync||!shared_queue||!shared_queue_sync||!queue_layout||
       !bar0||!static_info||!static_info_capacity||!out_static_info_size||
       !scratch||!timeout_ms||!poll_interval_ms||!delay_ms||!queue_runtime||
       !gmcapi||!rpc_transport||!rpc_manager||!out_result)
        return NVX_GSP_STARTUP_INVALID_ARGUMENT;

    /*
     * This transaction contains GA10x-specific firmware and register steps.
     * Refuse unknown NVIDIA IDs before touching queues, DMA, firmware, or BAR0.
     */
    if (!nvx_match_pci_device(platform->pci_device) ||
        !nvx_identify_pci_device(platform->pci_device->device_id,
                                 &family, &profile) ||
        family != NVX_FAMILY_AMPERE ||
        profile != NVX_PROFILE_GEFORCE_RTX_3060)
        return NVX_GSP_STARTUP_INVALID_ARGUMENT;

    if(overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,platform,sizeof(*platform))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,platform->pci_device,
          sizeof(*platform->pci_device))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,platform->trusted_gpu_facts,
          sizeof(*platform->trusted_gpu_facts))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,staged_vbios,sizeof(*staged_vbios))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,staged_vbios_sync,sizeof(*staged_vbios_sync))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,fwsec_info,sizeof(*fwsec_info))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,boot_memory,sizeof(*boot_memory))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,init_args,sizeof(*init_args))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,init_args_sync,sizeof(*init_args_sync))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,shared_queue,sizeof(*shared_queue))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,shared_queue_sync,sizeof(*shared_queue_sync))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,queue_layout,sizeof(*queue_layout))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,bar0,sizeof(*bar0))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,static_info,static_info_capacity)||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,out_static_info_size,sizeof(*out_static_info_size))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,scratch,scratch_size)||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,queue_runtime,sizeof(*queue_runtime))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,gmcapi,sizeof(*gmcapi))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,rpc_transport,sizeof(*rpc_transport))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,rpc_manager,sizeof(*rpc_manager))||
       overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,out_result,sizeof(*out_result))||
       dma_overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,staged_vbios)||
       dma_overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,init_args)||
       dma_overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,shared_queue)||
       boot_memory_overlap(platform->encoded_system_info_buffer,
          platform->encoded_system_info_capacity,boot_memory))
        return NVX_GSP_STARTUP_INVALID_ARGUMENT;

    *out_static_info_size=0U;
    memset(out_result,0,sizeof(*out_result));
    si_status=nvx_gsp_system_info_collect_pci(platform->pci_device,
        platform->trusted_gpu_facts,&collected);
    if(si_status==NVX_GSP_NVKV_OK)
        si_status=nvx_gsp_system_info_encode(&collected,
            platform->encoded_system_info_buffer,
            platform->encoded_system_info_capacity,&encoded_size);
    if(si_status!=NVX_GSP_NVKV_OK) {
        out_result->status=NVX_GSP_STARTUP_INVALID_ARGUMENT;
        return out_result->status;
    }

    return nvx_gsp_ga10x_start_and_init_rm(
        staged_vbios,staged_vbios_sync,fwsec_info,fuse_version,boot_memory,
        init_args,init_args_sync,shared_queue,shared_queue_sync,queue_layout,
        bar0,frts_framebuffer_offset,platform->encoded_system_info_buffer,
        encoded_size,static_info,static_info_capacity,out_static_info_size,
        scratch,scratch_size,timeout_ms,poll_interval_ms,delay_ms,context,
        queue_runtime,gmcapi,rpc_transport,rpc_manager,out_result);
}
