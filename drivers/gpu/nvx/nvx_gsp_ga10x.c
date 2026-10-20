/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_ga10x.h"
#include <string.h>
static int overlaps(const volatile void *a,size_t an,const volatile void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!an||!bn)return 0;
    if(x>UINTPTR_MAX-an||y>UINTPTR_MAX-bn)return 1;
    return x<y+bn&&y<x+an;
}
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

nvx_gsp_ga10x_status_t nvx_gsp_ga10x_setup(nvx_gsp_ga10x_t *g,
    const nvx_mmio_region_t *bar0)
{
    if(!g||!bar0||!bar0->base)return NVX_GSP_GA10X_INVALID_ARGUMENT;
    if(g->mmio_ready||g->command_ready||g->response_ready||g->rpc_ready)
        return NVX_GSP_GA10X_ALREADY_INITIALIZED;
    if(bar0->length<NVX_GSP_GA10X_REQUIRED_BAR_BYTES||
       overlaps(g,sizeof(*g),bar0,sizeof(*bar0))||
       overlaps(g,sizeof(*g),bar0->base,bar0->length))
        return NVX_GSP_GA10X_BAD_BAR;
    memset(g,0,sizeof(*g));
    if(nvx_gsp_msgq_io_init_mmio(&g->io,&g->mmio_context,bar0)!=NVX_GSP_MSGQ_QUEUE_OK) {
        memset(g,0,sizeof(*g));return NVX_GSP_GA10X_BAD_BAR;
    }
    g->mmio_ready=1U;

    /*
     * A readable BAR is not enough: GA102+ can return a fabricated pattern
     * while the GSP PRIV target remains locked. Refuse queue use in that state
     * rather than publishing a queue that cannot communicate with the engine.
     */
    nvx_gsp_falcon_status_t falcon_status =
        nvx_gsp_ga10x_inspect_falcon(bar0, &g->falcon);
    if (falcon_status != NVX_GSP_FALCON_OK) {
        memset(g, 0, sizeof(*g));
        if (falcon_status == NVX_GSP_FALCON_PRIVILEGED_WINDOW_LOCKED)
            return NVX_GSP_GA10X_PRIVILEGED_WINDOW_LOCKED;
        if (falcon_status == NVX_GSP_FALCON_MMIO_ERROR)
            return NVX_GSP_GA10X_MMIO_ERROR;
        return NVX_GSP_GA10X_BAD_BAR;
    }
    return NVX_GSP_GA10X_OK;
}
nvx_gsp_ga10x_status_t nvx_gsp_ga10x_create_command_queue(
    nvx_gsp_ga10x_t *g,void *store,size_t bytes)
{
    nvx_gsp_msgq_queue_status_t s;
    if(!g||!store)return NVX_GSP_GA10X_INVALID_ARGUMENT;
    if(!g->mmio_ready)return NVX_GSP_GA10X_QUEUE_NOT_READY;
    if(g->command_ready)return NVX_GSP_GA10X_ALREADY_INITIALIZED;
    if(overlaps(g,sizeof(*g),store,bytes)||
       overlaps(g->mmio_context.region->base,g->mmio_context.region->length,store,bytes))
        return NVX_GSP_GA10X_INVALID_ARGUMENT;
    s=nvx_gsp_msgq_queue_init_tx(&g->command_queue,store,bytes,
        NVX_GSP_PAGE_SIZE,&g->io,NVX_GSP_GA10X_QUEUE_HEAD(NVX_GSP_GA10X_RM_QUEUE_INDEX),
        NVX_GSP_GA10X_QUEUE_TAIL(NVX_GSP_GA10X_RM_QUEUE_INDEX));
    if(s!=NVX_GSP_MSGQ_QUEUE_OK)return NVX_GSP_GA10X_QUEUE_ERROR;
    g->command_ready=1U;return NVX_GSP_GA10X_OK;
}
nvx_gsp_ga10x_status_t nvx_gsp_ga10x_link_response_queue(
    nvx_gsp_ga10x_t *g,void *store,size_t bytes)
{
    nvx_gsp_msgq_queue_status_t s;
    if(!g||!store)return NVX_GSP_GA10X_INVALID_ARGUMENT;
    if(!g->command_ready)return NVX_GSP_GA10X_QUEUE_NOT_READY;
    if(g->response_ready)return NVX_GSP_GA10X_ALREADY_INITIALIZED;
    if(overlaps(g,sizeof(*g),store,bytes)||
       overlaps(g->mmio_context.region->base,g->mmio_context.region->length,store,bytes)||
       overlaps(g->command_queue.backing_store,g->command_queue.layout.backing_store_size,store,bytes))
        return NVX_GSP_GA10X_INVALID_ARGUMENT;
    s=nvx_gsp_msgq_queue_init_rx(&g->response_queue,store,bytes,
        NVX_GSP_PAGE_SIZE,&g->io,NVX_GSP_GA10X_MSGQ_HEAD(NVX_GSP_GA10X_RM_QUEUE_INDEX),
        NVX_GSP_GA10X_MSGQ_TAIL(NVX_GSP_GA10X_RM_QUEUE_INDEX));
    if(s!=NVX_GSP_MSGQ_QUEUE_OK)return NVX_GSP_GA10X_QUEUE_ERROR;
    g->response_ready=1U;return NVX_GSP_GA10X_OK;
}
nvx_gsp_ga10x_status_t nvx_gsp_ga10x_init_rpc(nvx_gsp_ga10x_t *g)
{
    if(!g)return NVX_GSP_GA10X_INVALID_ARGUMENT;
    if(!g->command_ready||!g->response_ready)return NVX_GSP_GA10X_QUEUE_NOT_READY;
    if(g->rpc_ready)return NVX_GSP_GA10X_ALREADY_INITIALIZED;
    if(nvx_gsp_msgq_rpc_init(&g->rpc,&g->command_queue,&g->response_queue)!=NVX_GSP_MSGQ_RPC_OK)
        return NVX_GSP_GA10X_QUEUE_ERROR;
    g->rpc_ready=1U;return NVX_GSP_GA10X_OK;
}
