/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_startup.h"
#include <string.h>
#include <stdint.h>

static int startup_overlap(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x=(uintptr_t)a, y=(uintptr_t)b;
    if (!an || !bn) return 0;
    if (!a || !b || x>UINTPTR_MAX-an || y>UINTPTR_MAX-bn) return 1;
    return x<y+bn && y<x+an;
}

nvx_gsp_startup_status_t nvx_gsp_ga10x_start_and_init_rm(
    nvx_dma_buffer_t *staged_vbios, nvx_dma_sync_t *staged_vbios_sync,
    const nvx_gsp_vbios_fwsec_info_t *fwsec_info, uint32_t fuse_version,
    nvx_gsp_boot_memory_t *boot_memory,
    nvx_dma_buffer_t *init_args, nvx_dma_sync_t *init_args_sync,
    nvx_dma_buffer_t *shared_queue, nvx_dma_sync_t *shared_queue_sync,
    const nvx_gsp_modern_shared_queue_layout_t *queue_layout,
    const nvx_mmio_region_t *bar0, uint64_t frts_framebuffer_offset,
    const void *encoded_system_info, size_t encoded_system_info_size,
    void *static_info, size_t static_info_capacity, size_t *out_static_info_size,
    void *scratch, size_t scratch_size,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context,
    nvx_gsp_msgq_ga10x_t *queue_runtime,
    nvx_gsp_gmcapi_transport_t *gmcapi,
    nvx_gsp_startup_result_t *out_result)
{
    nvx_gsp_startup_result_t result = {0};
    nvx_gsp_falcon_status_t boot_status;

    if (!staged_vbios || !staged_vbios_sync || !fwsec_info || !boot_memory ||
        !init_args || !init_args_sync || !shared_queue || !shared_queue_sync ||
        !queue_layout || !bar0 || !encoded_system_info ||
        !encoded_system_info_size || !static_info || !static_info_capacity ||
        !out_static_info_size || !scratch || !timeout_ms ||
        !poll_interval_ms || !delay_ms || !queue_runtime || !gmcapi ||
        !out_result ||
        startup_overlap(out_result,sizeof(*out_result),out_static_info_size,
                        sizeof(*out_static_info_size)) ||
        startup_overlap(out_result,sizeof(*out_result),static_info,
                        static_info_capacity) ||
        startup_overlap(out_result,sizeof(*out_result),scratch,scratch_size) ||
        startup_overlap(out_static_info_size,sizeof(*out_static_info_size),
                        static_info,static_info_capacity) ||
        startup_overlap(out_static_info_size,sizeof(*out_static_info_size),
                        scratch,scratch_size))
        return NVX_GSP_STARTUP_INVALID_ARGUMENT;

    memset(out_result, 0, sizeof(*out_result));
    *out_static_info_size = 0U;
    result.status = NVX_GSP_STARTUP_INVALID_ARGUMENT;
    if (shared_queue_sync->initialized &&
        shared_queue_sync->owner != NVX_DMA_OWNER_CPU) {
        result.status = NVX_GSP_STARTUP_INVALID_ARGUMENT;
        *out_result = result;
        return result.status;
    }

    result.queue_status = nvx_gsp_msgq_ga10x_prepare_command(
        queue_runtime, shared_queue, queue_layout, bar0);
    if (result.queue_status != NVX_GSP_MSGQ_GA10X_OK) {
        result.status = NVX_GSP_STARTUP_QUEUE_PREPARE_FAILED;
        *out_result = result;
        return result.status;
    }

    boot_status = nvx_gsp_ga10x_bootstrap_traced(
        staged_vbios, staged_vbios_sync, fwsec_info, fuse_version,
        boot_memory, init_args, init_args_sync, shared_queue, shared_queue_sync,
        bar0, frts_framebuffer_offset, timeout_ms, poll_interval_ms,
        delay_ms, context, &result.frts_result, &result.booter_mailbox1,
        &result.boot_trace);
    result.bootstrap_status = boot_status;
    if (boot_status != NVX_GSP_FALCON_OK) {
        result.status = NVX_GSP_STARTUP_BOOTSTRAP_FAILED;
        *out_result = result;
        return result.status;
    }

    result.queue_status = nvx_gsp_msgq_ga10x_link_status(
        queue_runtime, shared_queue, shared_queue_sync, queue_layout,
        timeout_ms, poll_interval_ms, delay_ms, context);
    if (result.queue_status != NVX_GSP_MSGQ_GA10X_OK) {
        result.status = NVX_GSP_STARTUP_STATUS_LINK_FAILED;
        *out_result = result;
        return result.status;
    }

    result.gmcapi_status = nvx_gsp_gmcapi_init(
        gmcapi, &queue_runtime->command_queue, &queue_runtime->status_queue);
    if (result.gmcapi_status != NVX_GSP_GMCAPI_OK) {
        result.status = NVX_GSP_STARTUP_GMCAPI_INIT_FAILED;
        *out_result = result;
        return result.status;
    }

    result.gmcapi_status = nvx_gsp_gmcapi_gsp_init(
        gmcapi, encoded_system_info, encoded_system_info_size,
        static_info, static_info_capacity, &result.static_info_size,
        &result.gsp_init_remote_status, scratch, scratch_size,
        timeout_ms, poll_interval_ms, delay_ms, context);
    *out_static_info_size = result.static_info_size;
    if (result.gmcapi_status != NVX_GSP_GMCAPI_OK) {
        result.status = NVX_GSP_STARTUP_GSP_INIT_FAILED;
        *out_result = result;
        return result.status;
    }

    result.gsp_rm_initialized = 1U;
    result.status = NVX_GSP_STARTUP_OK;
    *out_result = result;
    return NVX_GSP_STARTUP_OK;
}
