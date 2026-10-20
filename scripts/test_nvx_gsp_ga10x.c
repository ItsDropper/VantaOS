/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_ga10x.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
static uint8_t bar[NVX_GSP_GA10X_REQUIRED_BAR_BYTES] __attribute__((aligned(4)));
static uint8_t fuse_bar[NVX_GSP_GA10X_FUSE_GSP_UCODE1_VERSION +
                        NVX_GSP_GA10X_FUSE_UCODE_COUNT * sizeof(uint32_t)]
    __attribute__((aligned(4)));
static uint8_t command_store[8U*NVX_GSP_PAGE_SIZE] __attribute__((aligned(NVX_GSP_PAGE_SIZE)));
static uint8_t response_store[8U*NVX_GSP_PAGE_SIZE] __attribute__((aligned(NVX_GSP_PAGE_SIZE)));
int main(void)
{
 nvx_gsp_ga10x_t g={0};nvx_mmio_region_t region={bar,sizeof(bar)};
 nvx_mmio_region_t fuse_region={fuse_bar,sizeof(fuse_bar)};
 uint32_t fuse_version=UINT32_MAX;
 nvx_gsp_msgq_layout_t response_header={0};uint8_t *slot=0;uint32_t reg_value=UINT32_MAX;const char request[]="GSP queue 0";
 /* Fuse versions are the highest set bit + 1, not the raw register value. */
 assert(nvx_gsp_ga10x_read_ucode_fuse_version(&fuse_region,
     NVX_GSP_GA10X_ENGINE_MASK_GSP,1U,&fuse_version)==NVX_GSP_GA10X_OK);
 assert(fuse_version==0U);
 assert(nvx_mmio_write32(&fuse_region,NVX_GSP_GA10X_FUSE_GSP_UCODE1_VERSION,
     0x00000008U)==NVX_MMIO_OK);
 assert(nvx_gsp_ga10x_read_ucode_fuse_version(&fuse_region,
     NVX_GSP_GA10X_ENGINE_MASK_GSP,1U,&fuse_version)==NVX_GSP_GA10X_OK);
 assert(fuse_version==4U);
 assert(nvx_mmio_write32(&fuse_region,NVX_GSP_GA10X_FUSE_SEC2_UCODE1_VERSION+4U,
     0x00000002U)==NVX_MMIO_OK);
 assert(nvx_gsp_ga10x_read_ucode_fuse_version(&fuse_region,
     NVX_GSP_GA10X_ENGINE_MASK_SEC2,2U,&fuse_version)==NVX_GSP_GA10X_OK);
 assert(fuse_version==2U);
 assert(nvx_mmio_write32(&fuse_region,NVX_GSP_GA10X_FUSE_NVDEC_UCODE1_VERSION+60U,
     0x00008000U)==NVX_MMIO_OK);
 assert(nvx_gsp_ga10x_read_ucode_fuse_version(&fuse_region,
     NVX_GSP_GA10X_ENGINE_MASK_NVDEC,16U,&fuse_version)==NVX_GSP_GA10X_OK);
 assert(fuse_version==16U);
 assert(nvx_gsp_ga10x_read_ucode_fuse_version(&fuse_region,0x0005U,1U,
     &fuse_version)==NVX_GSP_GA10X_FUSE_UNSUPPORTED_ENGINE);
 assert(nvx_gsp_ga10x_read_ucode_fuse_version(&fuse_region,
     NVX_GSP_GA10X_ENGINE_MASK_GSP,0U,&fuse_version)==NVX_GSP_GA10X_FUSE_INVALID_UCODE_ID);
 assert(nvx_gsp_ga10x_read_ucode_fuse_version(&fuse_region,
     NVX_GSP_GA10X_ENGINE_MASK_GSP,17U,&fuse_version)==NVX_GSP_GA10X_FUSE_INVALID_UCODE_ID);
 nvx_mmio_region_t short_fuse_region={fuse_bar,
     NVX_GSP_GA10X_FUSE_GSP_UCODE1_VERSION+4U-1U};
 assert(nvx_gsp_ga10x_read_ucode_fuse_version(&short_fuse_region,
     NVX_GSP_GA10X_ENGINE_MASK_GSP,1U,&fuse_version)==NVX_GSP_GA10X_BAD_BAR);
 assert(fuse_version==0U);
 assert(nvx_mmio_write32(&fuse_region,NVX_GSP_GA10X_FUSE_GSP_UCODE1_VERSION,
     0x00010001U)==NVX_MMIO_OK);
 assert(nvx_gsp_ga10x_read_ucode_fuse_version(&fuse_region,
     NVX_GSP_GA10X_ENGINE_MASK_GSP,1U,&fuse_version)==NVX_GSP_GA10X_FUSE_RESERVED_BITS);
 assert(fuse_version==0U);
 assert(NVX_GSP_GA10X_QUEUE_HEAD(0U)==0x110C00U);
 assert(NVX_GSP_GA10X_QUEUE_TAIL(0U)==0x110C04U);
 assert(NVX_GSP_GA10X_MSGQ_HEAD(0U)==0x110C80U);
 assert(NVX_GSP_GA10X_MSGQ_TAIL(0U)==0x110C84U);
 assert(nvx_gsp_ga10x_create_command_queue(&g,command_store,sizeof(command_store))==NVX_GSP_GA10X_QUEUE_NOT_READY);
 nvx_mmio_region_t short_region={bar,NVX_GSP_GA10X_MSGQ_REQUIRED_BAR_BYTES-1U};
 assert(nvx_gsp_ga10x_setup(&g,&short_region)==NVX_GSP_GA10X_BAD_BAR);
 nvx_mmio_region_t locked_region={bar,sizeof(bar)};
 assert(nvx_mmio_write32(&locked_region,NVX_GSP_FALCON_HWCFG2,0xBADF41A5U)==NVX_MMIO_OK);
 assert(nvx_gsp_ga10x_setup(&g,&region)==NVX_GSP_GA10X_PRIVILEGED_WINDOW_LOCKED);
 assert(!g.mmio_ready&&!g.command_ready&&!g.response_ready&&!g.rpc_ready);
 assert(nvx_mmio_write32(&locked_region,NVX_GSP_FALCON_HWCFG2,0U)==NVX_MMIO_OK);
 assert(nvx_gsp_ga10x_setup(&g,&region)==NVX_GSP_GA10X_OK);
 assert(nvx_gsp_ga10x_setup(&g,&region)==NVX_GSP_GA10X_ALREADY_INITIALIZED);
 assert(nvx_gsp_ga10x_link_response_queue(&g,response_store,sizeof(response_store))==NVX_GSP_GA10X_QUEUE_NOT_READY);
 assert(nvx_gsp_ga10x_create_command_queue(&g,command_store,sizeof(command_store))==NVX_GSP_GA10X_OK);
 assert(g.command_queue.layout.element_count==7U);
 assert(nvx_mmio_read32(&region,NVX_GSP_GA10X_QUEUE_HEAD(0U),&reg_value)==NVX_MMIO_OK&&reg_value==0U);
 assert(nvx_gsp_msgq_tx_get(&g.command_queue,0U,&slot)==NVX_GSP_MSGQ_QUEUE_OK);
 memcpy(slot,request,sizeof(request));
 assert(nvx_gsp_msgq_tx_submit(&g.command_queue,1U)==NVX_GSP_MSGQ_QUEUE_OK);
 assert(nvx_mmio_read32(&region,NVX_GSP_GA10X_QUEUE_HEAD(0U),&reg_value)==NVX_MMIO_OK&&reg_value==1U);
 /* Before GSP creates the response header, link must fail without publishing readiness. */
 assert(nvx_gsp_ga10x_link_response_queue(&g,response_store,sizeof(response_store))==NVX_GSP_GA10X_QUEUE_ERROR);
 assert(!g.response_ready);
 assert(nvx_gsp_msgq_prepare_tx(response_store,sizeof(response_store),NVX_GSP_PAGE_SIZE,&response_header)==NVX_GSP_MSGQ_OK);
 assert(nvx_gsp_ga10x_link_response_queue(&g,response_store,sizeof(response_store))==NVX_GSP_GA10X_OK);
 assert(nvx_mmio_read32(&region,NVX_GSP_GA10X_MSGQ_TAIL(0U),&reg_value)==NVX_MMIO_OK&&reg_value==0U);
 assert(nvx_gsp_ga10x_init_rpc(&g)==NVX_GSP_GA10X_OK);
 assert(nvx_gsp_ga10x_init_rpc(&g)==NVX_GSP_GA10X_ALREADY_INITIALIZED);
 assert(g.rpc_ready&&g.rpc.command_queue==&g.command_queue&&g.rpc.response_queue==&g.response_queue);
 return 0;
}
