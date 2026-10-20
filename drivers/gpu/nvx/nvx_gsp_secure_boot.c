/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 *
 * The high-secure transfer order and image-field interpretation are based on
 * the public GA102 SEC2/Falcon boot contract in NVIDIA's MIT-licensed
 * open-gpu-kernel-modules and Linux Nova GA102 Falcon HAL.
 */
#include "nvx_gsp_secure_boot.h"

#include <string.h>

static int ranges_overlap(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (!an || !bn) return 0;
    if (x > UINTPTR_MAX - an || y > UINTPTR_MAX - bn) return 1;
    return x < y + bn && y < x + an;
}

static int range_within(uint32_t offset, uint32_t length, uint32_t limit)
{
    return offset <= limit && length <= limit - offset;
}

static int aligned_256(uint32_t value)
{
    return (value & (NVX_GA10X_FALCON_DMA_CHUNK_BYTES - 1U)) == 0U;
}

nvx_gsp_falcon_status_t nvx_gsp_ga10x_secure_image_plan(
    const nvx_dma_buffer_t *staged_image,
    const nvx_gsp_image_info_t *image,
    nvx_gsp_secure_transfer_plan_t *out_plan)
{
    nvx_gsp_secure_transfer_plan_t plan;

    if (!staged_image || !image || !out_plan ||
        ranges_overlap(staged_image, sizeof(*staged_image),
                       out_plan, sizeof(*out_plan)) ||
        ranges_overlap(image, sizeof(*image), out_plan, sizeof(*out_plan)) ||
        (staged_image->cpu_address &&
         ranges_overlap(staged_image->cpu_address, staged_image->allocated_size,
                        out_plan, sizeof(*out_plan))))
        return NVX_GSP_FALCON_INVALID_ARGUMENT;
    memset(out_plan, 0, sizeof(*out_plan));

    if (!staged_image->acquired || !staged_image->cpu_address ||
        !staged_image->gpu_address || !staged_image->size ||
        staged_image->allocated_size < staged_image->size ||
        !image->data_size || image->data_size > staged_image->size ||
        image->app_count != 1U || !image->app0_size ||
        !image->os_data_size || !image->signature_size ||
        image->signature_count == 0U || image->signature_count > NVX_GSP_IMAGE_MAX_SIGNATURES ||
        image->patch_signature != 0U ||
        image->os_code_size != image->app0_offset ||
        !range_within(image->app0_offset, image->app0_size, image->data_size) ||
        !range_within(image->os_data_offset, image->os_data_size, image->data_size) ||
        (uint64_t)image->app0_offset + image->app0_size > image->os_data_offset ||
        !range_within(image->patch_location, image->signature_size, image->data_size) ||
        image->patch_location < image->os_data_offset ||
        !range_within(image->patch_location - image->os_data_offset,
                      image->signature_size, image->os_data_size) ||
        !aligned_256(image->app0_offset) || !aligned_256(image->app0_size) ||
        !aligned_256(image->os_data_offset) || !aligned_256(image->os_data_size) ||
        ((image->patch_location - image->os_data_offset) & 3U) != 0U ||
        (staged_image->gpu_address & 0xFFU) != 0U ||
        staged_image->gpu_address >= (UINT64_C(1) << 49U) ||
        image->data_size > (UINT64_C(1) << 49U) - staged_image->gpu_address)
        return NVX_GSP_FALCON_IMAGE_INVALID;

    if ((image->engine_id != 0x0001U && image->engine_id != 0x0400U) ||
        image->ucode_id == 0U ||
        image->ucode_id > NVX_GSP_FALCON_BROM_UCODE_COUNT)
        return NVX_GSP_FALCON_BROM_INVALID_PARAMETERS;

    plan.source_phys = staged_image->gpu_address;
    plan.imem_source_offset = image->app0_offset;
    plan.imem_destination_offset = 0U;
    plan.imem_size = image->app0_size;
    plan.dmem_source_offset = image->os_data_offset;
    plan.dmem_destination_offset = 0U;
    plan.dmem_size = image->os_data_size;
    plan.boot_vector = image->app0_offset;
    plan.signature_dmem_offset = image->patch_location - image->os_data_offset;
    plan.engine_id_mask = image->engine_id;
    plan.ucode_id = image->ucode_id;
    *out_plan = plan;
    return NVX_GSP_FALCON_OK;
}


static nvx_gsp_falcon_status_t reset_and_reclaim(
    nvx_gsp_boot_memory_t *memory, const nvx_mmio_region_t *bar0,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context,
    uint8_t reclaim_load, uint8_t reclaim_metadata)
{
    nvx_gsp_falcon_status_t status = nvx_ga10x_falcon_reset(
        bar0, NVX_SEC2_FALCON_BASE, timeout_ms, poll_interval_ms,
        delay_ms, context);
    if (status != NVX_GSP_FALCON_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;

    if (reclaim_load &&
        memory->booter_load_sync.owner == NVX_DMA_OWNER_DEVICE &&
        nvx_dma_sync_for_cpu(&memory->booter_load_sync, 0U,
                             memory->booter_load.size) != NVX_DMA_SYNC_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    if (reclaim_metadata &&
        memory->wpr_metadata_sync.owner == NVX_DMA_OWNER_DEVICE &&
        nvx_dma_sync_for_cpu(&memory->wpr_metadata_sync, 0U,
                             memory->wpr_metadata.size) != NVX_DMA_SYNC_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    return NVX_GSP_FALCON_OK;
}

nvx_gsp_falcon_status_t nvx_gsp_ga10x_execute_booter_load(
    nvx_gsp_boot_memory_t *memory, const nvx_mmio_region_t *bar0,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context,
    uint32_t *out_mailbox1)
{
    nvx_gsp_secure_transfer_plan_t plan;
    nvx_gsp_falcon_status_t status;
    uint32_t mailbox0 = 0U, mailbox1 = 0U;

    if (!memory || !bar0 || !bar0->base || !delay_ms || !out_mailbox1 ||
        timeout_ms == 0U || poll_interval_ms == 0U ||
        ranges_overlap(memory, sizeof(*memory), bar0, sizeof(*bar0)) ||
        ranges_overlap(memory, sizeof(*memory), out_mailbox1,
                       sizeof(*out_mailbox1)) ||
        ranges_overlap((const void *)bar0->base, bar0->length, out_mailbox1,
                       sizeof(*out_mailbox1)) ||
        ranges_overlap(memory->booter_load.cpu_address,
                       memory->booter_load.allocated_size, out_mailbox1,
                       sizeof(*out_mailbox1)) ||
        ranges_overlap(memory->wpr_metadata.cpu_address,
                       memory->wpr_metadata.allocated_size, out_mailbox1,
                       sizeof(*out_mailbox1)))
        return NVX_GSP_FALCON_INVALID_ARGUMENT;
    *out_mailbox1 = 0U;

    if (!memory->acquired || !memory->booter_load.acquired ||
        !memory->booter_load_sync.initialized ||
        memory->booter_load_sync.owner != NVX_DMA_OWNER_CPU)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    if (!memory->booters_staged)
        return NVX_GSP_FALCON_BOOTER_NOT_STAGED;
    if (!memory->wpr_metadata_prepared || !memory->wpr_metadata.acquired ||
        !memory->wpr_metadata_sync.initialized ||
        memory->wpr_metadata_sync.owner != NVX_DMA_OWNER_CPU ||
        memory->wpr_metadata.gpu_address == 0U ||
        (memory->wpr_metadata.gpu_address & (NVX_DMA_PAGE_SIZE - 1U)) != 0U ||
        memory->wpr_metadata.gpu_address >= (UINT64_C(1) << 49U))
        return NVX_GSP_FALCON_WPR_METADATA_NOT_READY;

    status = nvx_gsp_ga10x_secure_image_plan(
        &memory->booter_load, &memory->booter_load_info, &plan);
    if (status != NVX_GSP_FALCON_OK)
        return status;
    /* GA10x Booter Load runs on SEC2, not the GSP Falcon. */
    if (plan.engine_id_mask != 0x0001U)
        return NVX_GSP_FALCON_BROM_INVALID_PARAMETERS;

    status = nvx_ga10x_falcon_reset(bar0, NVX_SEC2_FALCON_BASE,
        timeout_ms, poll_interval_ms, delay_ms, context);
    if (status != NVX_GSP_FALCON_OK)
        return status;

    if (nvx_dma_sync_for_device(&memory->booter_load_sync, 0U,
                                memory->booter_load.size) != NVX_DMA_SYNC_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;

    status = nvx_ga10x_falcon_dma_upload(
        bar0, NVX_SEC2_FALCON_BASE, plan.source_phys,
        plan.imem_source_offset, plan.imem_destination_offset,
        plan.imem_size, NVX_GA10X_FALCON_SECURE_IMEM, 0U,
        timeout_ms, poll_interval_ms, delay_ms, context);
    if (status != NVX_GSP_FALCON_OK) {
        nvx_gsp_falcon_status_t reclaim = reset_and_reclaim(
            memory, bar0, timeout_ms, poll_interval_ms, delay_ms, context, 1U, 0U);
        return reclaim == NVX_GSP_FALCON_OK ? status : reclaim;
    }

    status = nvx_ga10x_falcon_dma_upload(
        bar0, NVX_SEC2_FALCON_BASE, plan.source_phys,
        plan.dmem_source_offset, plan.dmem_destination_offset,
        plan.dmem_size, NVX_GA10X_FALCON_DMEM, 0U,
        timeout_ms, poll_interval_ms, delay_ms, context);
    if (status != NVX_GSP_FALCON_OK) {
        nvx_gsp_falcon_status_t reclaim = reset_and_reclaim(
            memory, bar0, timeout_ms, poll_interval_ms, delay_ms, context, 1U, 0U);
        return reclaim == NVX_GSP_FALCON_OK ? status : reclaim;
    }

    if (nvx_dma_sync_for_cpu(&memory->booter_load_sync, 0U,
                             memory->booter_load.size) != NVX_DMA_SYNC_OK) {
        (void)reset_and_reclaim(memory, bar0, timeout_ms, poll_interval_ms,
                                delay_ms, context, 1U, 0U);
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    }

    status = nvx_ga10x_falcon_program_brom(
        bar0, NVX_SEC2_FALCON_BASE, NVX_SEC2_FALCON2_BASE,
        plan.engine_id_mask, plan.ucode_id, plan.signature_dmem_offset);
    if (status != NVX_GSP_FALCON_OK)
        return status;

    if (nvx_dma_sync_for_device(&memory->wpr_metadata_sync, 0U,
                                memory->wpr_metadata.size) != NVX_DMA_SYNC_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;

    status = nvx_ga10x_falcon_boot(
        bar0, NVX_SEC2_FALCON_BASE, plan.boot_vector,
        1U, (uint32_t)memory->wpr_metadata.gpu_address,
        1U, (uint32_t)(memory->wpr_metadata.gpu_address >> 32U),
        &mailbox0, &mailbox1, timeout_ms, poll_interval_ms, delay_ms, context);
    if (status != NVX_GSP_FALCON_OK) {
        nvx_gsp_falcon_status_t reclaim = reset_and_reclaim(
            memory, bar0, timeout_ms, poll_interval_ms, delay_ms, context, 0U, 1U);
        return reclaim == NVX_GSP_FALCON_OK ? status : reclaim;
    }

    if (nvx_dma_sync_for_cpu(&memory->wpr_metadata_sync, 0U,
                             memory->wpr_metadata.size) != NVX_DMA_SYNC_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    if (mailbox0 != 0U)
        return NVX_GSP_FALCON_BOOTER_REPORTED_ERROR;

    *out_mailbox1 = mailbox1;
    return NVX_GSP_FALCON_OK;
}
