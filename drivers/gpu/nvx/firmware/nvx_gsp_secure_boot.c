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
#include "nvx_gsp_fwsec_result.h"
#include "nvx_gsp_fwsec_mmio.h"
#include "nvx_gsp_fwsec_stage.h"

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




static nvx_gsp_falcon_status_t reset_gsp_and_reclaim_vbios(
    const nvx_mmio_region_t *bar0, nvx_dma_buffer_t *buffer,
    nvx_dma_sync_t *sync, uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context)
{
    nvx_gsp_falcon_status_t status = nvx_ga10x_falcon_reset(
        bar0, NVX_GSP_FALCON_BASE, timeout_ms, poll_interval_ms,
        delay_ms, context);
    if (status != NVX_GSP_FALCON_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    if (sync->owner == NVX_DMA_OWNER_DEVICE &&
        nvx_dma_sync_for_cpu(sync, 0U, buffer->size) != NVX_DMA_SYNC_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    if (sync->owner != NVX_DMA_OWNER_CPU)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    return NVX_GSP_FALCON_OK;
}

nvx_gsp_falcon_status_t nvx_gsp_ga10x_execute_fwsec_frts(
    nvx_dma_buffer_t *staged_vbios, nvx_dma_sync_t *staged_vbios_sync,
    const nvx_gsp_vbios_fwsec_info_t *info, uint32_t fuse_version,
    const nvx_mmio_region_t *bar0, uint64_t frts_framebuffer_offset,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context,
    nvx_gsp_fwsec_result_snapshot_t *out_result)
{
    nvx_gsp_falcon_status_t status;
    nvx_gsp_fwsec_result_snapshot_t result = {0};
    nvx_gsp_fwsec_command_window_t window = {0};
    nvx_gsp_vbios_status_t vbios_status;
    nvx_gsp_fwsec_interface_status_t interface_status;
    uint8_t *dmem;
    uint64_t image_end, dmem_source_offset, dmem_source_phys;
    uint64_t imem_source_phys, signature_end, signatures_end;
    uint32_t signature_offset = 0U;
    uint32_t mailbox0 = 0U, mailbox1 = 0U;
    size_t interface_span;
    const size_t signature_size = NVX_GSP_VBIOS_RSA3K_SIGNATURE_SIZE;

    if (!staged_vbios || !staged_vbios_sync || !info || !bar0 ||
        !bar0->base || !out_result || !delay_ms ||
        timeout_ms == 0U || poll_interval_ms == 0U ||
        frts_framebuffer_offset == 0U ||
        (frts_framebuffer_offset & 0xFFFU) != 0U ||
        (frts_framebuffer_offset >> 12U) > 0x0FFFFFFFU ||
        ranges_overlap(out_result, sizeof(*out_result), staged_vbios,
                       sizeof(*staged_vbios)) ||
        ranges_overlap(out_result, sizeof(*out_result), staged_vbios_sync,
                       sizeof(*staged_vbios_sync)) ||
        ranges_overlap(out_result, sizeof(*out_result), info, sizeof(*info)) ||
        ranges_overlap(out_result, sizeof(*out_result), bar0, sizeof(*bar0)) ||
        ranges_overlap(out_result, sizeof(*out_result),
                       (const void *)bar0->base, bar0->length) ||
        ranges_overlap(out_result, sizeof(*out_result),
                       staged_vbios->cpu_address,
                       staged_vbios->allocated_size))
        return NVX_GSP_FALCON_INVALID_ARGUMENT;

    memset(out_result, 0, sizeof(*out_result));
    if (bar0->length < NVX_GSP_FWSEC_MMIO_REQUIRED_BAR_BYTES)
        return NVX_GSP_FALCON_BAR_TOO_SMALL;

    if (!staged_vbios->acquired || !staged_vbios->cpu_address ||
        staged_vbios->size == 0U ||
        staged_vbios->allocated_size < staged_vbios->size ||
        staged_vbios->gpu_address == 0U ||
        !staged_vbios_sync->initialized ||
        staged_vbios_sync->owner != NVX_DMA_OWNER_CPU ||
        staged_vbios_sync->buffer != staged_vbios ||
        staged_vbios_sync->size != staged_vbios->size ||
        staged_vbios_sync->gpu_address != staged_vbios->gpu_address)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;

    if (info->descriptor_version != NVX_GSP_VBIOS_FWSEC_DESC_V3 ||
        info->encrypted != 0U ||
        info->engine_id_mask != NVX_GSP_VBIOS_FWSEC_ENGINE_MASK ||
        info->ucode_id == 0U ||
        info->ucode_id > NVX_GSP_FALCON_BROM_UCODE_COUNT ||
        info->image_size == 0U || info->imem_load_size == 0U ||
        info->dmem_load_size == 0U ||
        (info->imem_load_size & (NVX_GA10X_FALCON_DMA_CHUNK_BYTES - 1U)) != 0U ||
        (info->dmem_load_size & (NVX_GA10X_FALCON_DMA_CHUNK_BYTES - 1U)) != 0U ||
        (info->imem_virtual_base & (NVX_GA10X_FALCON_DMA_CHUNK_BYTES - 1U)) != 0U ||
        (info->imem_physical_base & (NVX_GA10X_FALCON_DMA_CHUNK_BYTES - 1U)) != 0U ||
        (info->dmem_physical_base & (NVX_GA10X_FALCON_DMA_CHUNK_BYTES - 1U)) != 0U ||
        info->image_offset < info->imem_virtual_base ||
        info->imem_load_size > info->image_size ||
        info->dmem_load_size > info->image_size - info->imem_load_size)
        return NVX_GSP_FALCON_IMAGE_INVALID;

    image_end = (uint64_t)info->image_offset + info->image_size;
    dmem_source_offset = (uint64_t)info->image_offset + info->imem_load_size;
    if (image_end > staged_vbios->size ||
        dmem_source_offset > image_end ||
        info->dmem_load_size > image_end - dmem_source_offset ||
        dmem_source_offset > SIZE_MAX ||
        (uint64_t)info->image_offset + info->imem_load_size > SIZE_MAX)
        return NVX_GSP_FALCON_IMAGE_INVALID;

    if (staged_vbios->gpu_address >
            UINT64_MAX - ((uint64_t)info->image_offset - info->imem_virtual_base) ||
        staged_vbios->gpu_address >
            UINT64_MAX - dmem_source_offset)
        return NVX_GSP_FALCON_DMA_INVALID_TRANSFER;
    imem_source_phys = staged_vbios->gpu_address +
                       info->image_offset - info->imem_virtual_base;
    dmem_source_phys = staged_vbios->gpu_address + dmem_source_offset;
    if ((imem_source_phys & 0xFFU) != 0U ||
        (dmem_source_phys & 0xFFU) != 0U)
        return NVX_GSP_FALCON_DMA_INVALID_TRANSFER;

    vbios_status = nvx_gsp_vbios_fwsec_signature_offset(
        info, fuse_version, &signature_offset);
    signatures_end = (uint64_t)info->signatures_offset + info->signatures_size;
    signature_end = (uint64_t)signature_offset + signature_size;
    if (vbios_status != NVX_GSP_VBIOS_OK ||
        signature_offset < info->signatures_offset ||
        signature_end > signatures_end ||
        signature_end > staged_vbios->size ||
        (uint64_t)info->pkc_data_offset + signature_size > info->dmem_load_size)
        return NVX_GSP_FALCON_IMAGE_INVALID;

    dmem = (uint8_t *)staged_vbios->cpu_address + (size_t)dmem_source_offset;
    interface_status = nvx_gsp_fwsec_find_command_window(
        dmem, info->dmem_load_size, info->interface_offset,
        NVX_GSP_FWSEC_CMD_FRTS, NVX_GSP_FWSEC_FRTS_COMMAND_SIZE, &window);
    if (interface_status != NVX_GSP_FWSEC_INTERFACE_OK)
        return NVX_GSP_FALCON_IMAGE_INVALID;

    interface_span = (size_t)dmem[info->interface_offset + 1U] +
        (size_t)dmem[info->interface_offset + 2U] *
        (size_t)dmem[info->interface_offset + 3U];
    if (ranges_overlap(dmem + info->pkc_data_offset, signature_size,
                       dmem + window.mapper_offset, window.mapper_size) ||
        ranges_overlap(dmem + info->pkc_data_offset, signature_size,
                       dmem + window.init_cmd_offset, sizeof(uint32_t)) ||
        ranges_overlap(dmem + info->pkc_data_offset, signature_size,
                       dmem + window.command_input_offset,
                       window.command_input_capacity) ||
        ranges_overlap(dmem + info->pkc_data_offset, signature_size,
                       dmem + info->interface_offset, interface_span))
        return NVX_GSP_FALCON_IMAGE_INVALID;

    if (nvx_gsp_fwsec_stage_command(
            dmem, info->dmem_load_size, info->interface_offset,
            NVX_GSP_FWSEC_CMD_FRTS, frts_framebuffer_offset, &window) !=
            NVX_GSP_FWSEC_STAGE_OK)
        return NVX_GSP_FALCON_IMAGE_INVALID;

    /* The selected RSA-3K signature is patched into the FWSEC DMEM image. */
    memcpy(dmem + info->pkc_data_offset,
           (const uint8_t *)staged_vbios->cpu_address + signature_offset,
           signature_size);

    status = nvx_ga10x_falcon_reset(
        bar0, NVX_GSP_FALCON_BASE, timeout_ms, poll_interval_ms,
        delay_ms, context);
    if (status != NVX_GSP_FALCON_OK)
        return status;

    if (nvx_dma_sync_for_device(staged_vbios_sync, 0U,
                                staged_vbios->size) != NVX_DMA_SYNC_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;

    status = nvx_ga10x_falcon_dma_upload(
        bar0, NVX_GSP_FALCON_BASE, imem_source_phys,
        info->imem_virtual_base, info->imem_physical_base,
        info->imem_load_size, NVX_GA10X_FALCON_SECURE_IMEM, 0U,
        timeout_ms, poll_interval_ms, delay_ms, context);
    if (status != NVX_GSP_FALCON_OK)
        goto recover;

    status = nvx_ga10x_falcon_dma_upload(
        bar0, NVX_GSP_FALCON_BASE, dmem_source_phys, 0U,
        info->dmem_physical_base, info->dmem_load_size,
        NVX_GA10X_FALCON_DMEM, 0U,
        timeout_ms, poll_interval_ms, delay_ms, context);
    if (status != NVX_GSP_FALCON_OK)
        goto recover;

    status = nvx_ga10x_falcon_program_brom(
        bar0, NVX_GSP_FALCON_BASE, NVX_GSP_FALCON2_BASE,
        info->engine_id_mask, info->ucode_id, info->pkc_data_offset);
    if (status != NVX_GSP_FALCON_OK)
        goto recover;

    status = nvx_ga10x_falcon_boot(
        bar0, NVX_GSP_FALCON_BASE, info->imem_virtual_base,
        0U, 0U, 0U, 0U, &mailbox0, &mailbox1,
        timeout_ms, poll_interval_ms, delay_ms, context);
    if (status != NVX_GSP_FALCON_OK)
        goto recover;

    /* Boot helper returned only after the Falcon halted: CPU may reclaim DMA. */
    if (nvx_dma_sync_for_cpu(staged_vbios_sync, 0U,
                             staged_vbios->size) != NVX_DMA_SYNC_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;

    if (nvx_gsp_fwsec_read_result_snapshot(
            bar0, frts_framebuffer_offset, &result) != NVX_GSP_FWSEC_MMIO_OK ||
        nvx_gsp_fwsec_validate_result(NVX_GSP_FWSEC_CMD_FRTS, &result) !=
            NVX_GSP_FWSEC_RESULT_OK)
        return NVX_GSP_FALCON_FWSEC_NOT_VERIFIED;

    *out_result = result;
    return NVX_GSP_FALCON_OK;

recover:
    if (reset_gsp_and_reclaim_vbios(
            bar0, staged_vbios, staged_vbios_sync, timeout_ms,
            poll_interval_ms, delay_ms, context) != NVX_GSP_FALCON_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    return status;
}

/*
 * GA10x/TU102 sequence: FWSEC-FRTS establishes WPR2 first; then GSP is reset
 * and started with the SYSRAM radix3 ELF address in its two boot mailboxes.
 * SEC2 Booter Load is a later step and intentionally remains a separate call.
 */


static nvx_gsp_falcon_status_t reset_gsp_and_reclaim_boot_buffers(
    nvx_gsp_boot_memory_t *memory, nvx_dma_buffer_t *libos_init_args,
    nvx_dma_sync_t *libos_init_args_sync, nvx_dma_buffer_t *shared_queue_mem,
    nvx_dma_sync_t *shared_queue_sync, const nvx_mmio_region_t *bar0,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context)
{
    nvx_gsp_falcon_status_t status = nvx_ga10x_falcon_reset(
        bar0, NVX_GSP_FALCON_BASE, timeout_ms, poll_interval_ms,
        delay_ms, context);
    if (status != NVX_GSP_FALCON_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;

    if (memory->radix3_elf_sync.owner == NVX_DMA_OWNER_DEVICE &&
        nvx_dma_sync_for_cpu(&memory->radix3_elf_sync, 0U,
                             memory->radix3_elf.size) != NVX_DMA_SYNC_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    if (libos_init_args_sync->owner == NVX_DMA_OWNER_DEVICE &&
        nvx_dma_sync_for_cpu(libos_init_args_sync, 0U,
                             libos_init_args->size) != NVX_DMA_SYNC_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    if (shared_queue_sync->owner == NVX_DMA_OWNER_DEVICE &&
        nvx_dma_sync_for_cpu(shared_queue_sync, 0U,
                             shared_queue_mem->size) != NVX_DMA_SYNC_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    if (memory->radix3_elf_sync.owner != NVX_DMA_OWNER_CPU ||
        libos_init_args_sync->owner != NVX_DMA_OWNER_CPU ||
        shared_queue_sync->owner != NVX_DMA_OWNER_CPU)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    return NVX_GSP_FALCON_OK;
}

nvx_gsp_falcon_status_t nvx_gsp_ga10x_prepare_riscv_after_frts(
    nvx_gsp_boot_memory_t *memory,
    nvx_dma_buffer_t *libos_init_args,
    nvx_dma_sync_t *libos_init_args_sync,
    nvx_dma_buffer_t *shared_queue_mem,
    nvx_dma_sync_t *shared_queue_sync,
    const nvx_mmio_region_t *bar0, uint64_t frts_framebuffer_offset,
    const nvx_gsp_fwsec_result_snapshot_t *frts_result,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context)
{
    nvx_gsp_falcon_status_t status;
    const nvx_gsp_arguments_cached_modern_t *arguments;

    if (!memory || !libos_init_args || !libos_init_args_sync ||
        !shared_queue_mem || !shared_queue_sync || !bar0 || !bar0->base ||
        !frts_result || !delay_ms || timeout_ms == 0U ||
        poll_interval_ms == 0U || frts_framebuffer_offset == 0U ||
        (frts_framebuffer_offset & 0xFFFU) != 0U ||
        (frts_framebuffer_offset >> 12U) > 0x0FFFFFFFU ||
        ranges_overlap(memory, sizeof(*memory), libos_init_args,
                       sizeof(*libos_init_args)) ||
        ranges_overlap(memory, sizeof(*memory), libos_init_args_sync,
                       sizeof(*libos_init_args_sync)) ||
        ranges_overlap(memory, sizeof(*memory), shared_queue_mem,
                       sizeof(*shared_queue_mem)) ||
        ranges_overlap(memory, sizeof(*memory), shared_queue_sync,
                       sizeof(*shared_queue_sync)) ||
        ranges_overlap(memory, sizeof(*memory), bar0, sizeof(*bar0)) ||
        ranges_overlap(memory, sizeof(*memory), (const void *)bar0->base,
                       bar0->length) ||
        ranges_overlap(libos_init_args, sizeof(*libos_init_args),
                       shared_queue_mem, sizeof(*shared_queue_mem)) ||
        ranges_overlap(libos_init_args_sync, sizeof(*libos_init_args_sync),
                       shared_queue_sync, sizeof(*shared_queue_sync)) ||
        ranges_overlap(libos_init_args, sizeof(*libos_init_args), bar0,
                       sizeof(*bar0)) ||
        ranges_overlap(shared_queue_mem, sizeof(*shared_queue_mem), bar0,
                       sizeof(*bar0)) ||
        ranges_overlap(libos_init_args, sizeof(*libos_init_args),
                       (const void *)bar0->base, bar0->length) ||
        ranges_overlap(shared_queue_mem, sizeof(*shared_queue_mem),
                       (const void *)bar0->base, bar0->length) ||
        ranges_overlap(libos_init_args->cpu_address,
                       libos_init_args->allocated_size,
                       memory->radix3_elf.cpu_address,
                       memory->radix3_elf.allocated_size) ||
        ranges_overlap(libos_init_args->cpu_address,
                       libos_init_args->allocated_size,
                       memory->booter_load.cpu_address,
                       memory->booter_load.allocated_size) ||
        ranges_overlap(libos_init_args->cpu_address,
                       libos_init_args->allocated_size,
                       memory->wpr_metadata.cpu_address,
                       memory->wpr_metadata.allocated_size) ||
        ranges_overlap(shared_queue_mem->cpu_address,
                       shared_queue_mem->allocated_size,
                       memory->radix3_elf.cpu_address,
                       memory->radix3_elf.allocated_size) ||
        ranges_overlap(shared_queue_mem->cpu_address,
                       shared_queue_mem->allocated_size,
                       memory->booter_load.cpu_address,
                       memory->booter_load.allocated_size) ||
        ranges_overlap(shared_queue_mem->cpu_address,
                       shared_queue_mem->allocated_size,
                       memory->wpr_metadata.cpu_address,
                       memory->wpr_metadata.allocated_size) ||
        ranges_overlap(shared_queue_mem->cpu_address,
                       shared_queue_mem->allocated_size,
                       libos_init_args->cpu_address,
                       libos_init_args->allocated_size))
        return NVX_GSP_FALCON_INVALID_ARGUMENT;

    if (nvx_gsp_fwsec_validate_result(NVX_GSP_FWSEC_CMD_FRTS, frts_result) !=
            NVX_GSP_FWSEC_RESULT_OK ||
        frts_result->expected_wpr2_addr_lo_units !=
            (uint32_t)(frts_framebuffer_offset >> 12U) ||
        frts_result->wpr2_addr_lo_units !=
            (uint32_t)(frts_framebuffer_offset >> 12U))
        return NVX_GSP_FALCON_FWSEC_NOT_VERIFIED;

    if (!memory->acquired || !memory->radix3_elf.acquired ||
        !memory->radix3_elf.cpu_address || memory->radix3_elf.size == 0U ||
        memory->radix3_elf.allocated_size < memory->radix3_elf.size ||
        memory->radix3_elf.gpu_address == 0U ||
        (memory->radix3_elf.gpu_address & (NVX_DMA_PAGE_SIZE - 1U)) != 0U ||
        memory->radix3_elf.gpu_address >= (UINT64_C(1) << 49U) ||
        !memory->radix3_elf_sync.initialized ||
        memory->radix3_elf_sync.owner != NVX_DMA_OWNER_CPU ||
        memory->radix3_elf_sync.buffer != &memory->radix3_elf ||
        !libos_init_args->acquired || !libos_init_args->cpu_address ||
        libos_init_args->size < NVX_GSP_INIT_ARGS_PAGE_SIZE ||
        libos_init_args->allocated_size < libos_init_args->size ||
        libos_init_args->gpu_address == 0U ||
        (libos_init_args->gpu_address & (NVX_GSP_INIT_ARGS_PAGE_SIZE - 1U)) != 0U ||
        libos_init_args->gpu_address >= (UINT64_C(1) << 49U) ||
        !libos_init_args_sync->initialized ||
        libos_init_args_sync->owner != NVX_DMA_OWNER_CPU ||
        libos_init_args_sync->buffer != libos_init_args ||
        libos_init_args_sync->size != libos_init_args->size ||
        libos_init_args_sync->gpu_address != libos_init_args->gpu_address ||
        !shared_queue_mem->acquired || !shared_queue_mem->cpu_address ||
        shared_queue_mem->size == 0U ||
        shared_queue_mem->allocated_size < shared_queue_mem->size ||
        shared_queue_mem->gpu_address == 0U ||
        (shared_queue_mem->gpu_address & (NVX_GSP_INIT_ARGS_PAGE_SIZE - 1U)) != 0U ||
        shared_queue_mem->gpu_address >= (UINT64_C(1) << 49U) ||
        !shared_queue_sync->initialized ||
        shared_queue_sync->owner != NVX_DMA_OWNER_CPU ||
        shared_queue_sync->buffer != shared_queue_mem ||
        shared_queue_sync->size != shared_queue_mem->size ||
        shared_queue_sync->gpu_address != shared_queue_mem->gpu_address)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;

    arguments = (const nvx_gsp_arguments_cached_modern_t *)
        libos_init_args->cpu_address;
    if (arguments->magic != NVX_GSP_INIT_ARGS_MAGIC ||
        arguments->size != sizeof(*arguments) ||
        arguments->message_queue.shared_mem_phys_addr !=
            shared_queue_mem->gpu_address ||
        arguments->message_queue.page_table_entry_count == 0U ||
        arguments->message_queue.page_table_entry_count >
            NVX_GSP_INIT_ARGS_PAGE_SIZE / sizeof(uint64_t) ||
        arguments->message_queue.command_queue_offset >= shared_queue_mem->size ||
        arguments->message_queue.status_queue_offset >= shared_queue_mem->size ||
        (arguments->message_queue.command_queue_offset &
         (NVX_GSP_INIT_ARGS_PAGE_SIZE - 1U)) != 0U ||
        (arguments->message_queue.status_queue_offset &
         (NVX_GSP_INIT_ARGS_PAGE_SIZE - 1U)) != 0U)
        return NVX_GSP_FALCON_IMAGE_INVALID;

    if (nvx_dma_sync_for_device(&memory->radix3_elf_sync, 0U,
                                memory->radix3_elf.size) != NVX_DMA_SYNC_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    if (nvx_dma_sync_for_device(libos_init_args_sync, 0U,
                                libos_init_args->size) != NVX_DMA_SYNC_OK) {
        (void)reset_gsp_and_reclaim_boot_buffers(
            memory, libos_init_args, libos_init_args_sync, shared_queue_mem,
            shared_queue_sync, bar0, timeout_ms, poll_interval_ms,
            delay_ms, context);
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    }
    if (nvx_dma_sync_for_device(shared_queue_sync, 0U,
                                shared_queue_mem->size) != NVX_DMA_SYNC_OK) {
        (void)reset_gsp_and_reclaim_boot_buffers(
            memory, libos_init_args, libos_init_args_sync, shared_queue_mem,
            shared_queue_sync, bar0, timeout_ms, poll_interval_ms,
            delay_ms, context);
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    }

    status = nvx_gsp_ga10x_reset_into_riscv(
        bar0, timeout_ms, poll_interval_ms, delay_ms, context);
    if (status != NVX_GSP_FALCON_OK) {
        nvx_gsp_falcon_status_t recovery = reset_gsp_and_reclaim_boot_buffers(
            memory, libos_init_args, libos_init_args_sync, shared_queue_mem,
            shared_queue_sync, bar0, timeout_ms, poll_interval_ms,
            delay_ms, context);
        return recovery == NVX_GSP_FALCON_OK ? status : recovery;
    }

    if (nvx_mmio_write32(bar0, NVX_GSP_FALCON_MAILBOX0,
                         (uint32_t)libos_init_args->gpu_address) != NVX_MMIO_OK ||
        nvx_mmio_write32(bar0, NVX_GSP_FALCON_MAILBOX1,
                         (uint32_t)(libos_init_args->gpu_address >> 32U)) !=
                         NVX_MMIO_OK) {
        nvx_gsp_falcon_status_t recovery = reset_gsp_and_reclaim_boot_buffers(
            memory, libos_init_args, libos_init_args_sync, shared_queue_mem,
            shared_queue_sync, bar0, timeout_ms, poll_interval_ms,
            delay_ms, context);
        return recovery == NVX_GSP_FALCON_OK
            ? NVX_GSP_FALCON_MMIO_ERROR : recovery;
    }
    return NVX_GSP_FALCON_OK;
}

nvx_gsp_falcon_status_t nvx_gsp_ga10x_bootstrap_traced(
    nvx_dma_buffer_t *staged_vbios, nvx_dma_sync_t *staged_vbios_sync,
    const nvx_gsp_vbios_fwsec_info_t *fwsec_info, uint32_t fuse_version,
    nvx_gsp_boot_memory_t *memory,
    nvx_dma_buffer_t *libos_init_args,
    nvx_dma_sync_t *libos_init_args_sync,
    nvx_dma_buffer_t *shared_queue_mem,
    nvx_dma_sync_t *shared_queue_sync,
    const nvx_mmio_region_t *bar0, uint64_t frts_framebuffer_offset,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context,
    nvx_gsp_fwsec_result_snapshot_t *out_frts_result,
    uint32_t *out_booter_mailbox1, nvx_gsp_boot_trace_t *out_trace)
{
    nvx_gsp_fwsec_result_snapshot_t frts_result = {0};
    nvx_gsp_boot_trace_t trace = {0};
    nvx_gsp_falcon_status_t status;
    uint32_t mailbox1 = 0U;

    if (!staged_vbios || !staged_vbios_sync || !fwsec_info || !memory ||
        !libos_init_args || !libos_init_args_sync || !shared_queue_mem ||
        !shared_queue_sync || !bar0 || !bar0->base || !out_frts_result ||
        !out_booter_mailbox1 || !out_trace || !delay_ms ||
        timeout_ms == 0U || poll_interval_ms == 0U ||
        ranges_overlap(out_frts_result, sizeof(*out_frts_result),
                       out_booter_mailbox1, sizeof(*out_booter_mailbox1)) ||
        ranges_overlap(out_frts_result, sizeof(*out_frts_result),
                       out_trace, sizeof(*out_trace)) ||
        ranges_overlap(out_booter_mailbox1, sizeof(*out_booter_mailbox1),
                       out_trace, sizeof(*out_trace)) ||
        ranges_overlap(out_trace, sizeof(*out_trace), memory, sizeof(*memory)) ||
        ranges_overlap(out_trace, sizeof(*out_trace), libos_init_args,
                       sizeof(*libos_init_args)) ||
        ranges_overlap(out_trace, sizeof(*out_trace), libos_init_args_sync,
                       sizeof(*libos_init_args_sync)) ||
        ranges_overlap(out_trace, sizeof(*out_trace), shared_queue_mem,
                       sizeof(*shared_queue_mem)) ||
        ranges_overlap(out_trace, sizeof(*out_trace), shared_queue_sync,
                       sizeof(*shared_queue_sync)) ||
        ranges_overlap(out_trace, sizeof(*out_trace), bar0, sizeof(*bar0)) ||
        ranges_overlap(out_trace, sizeof(*out_trace),
                       (const void *)bar0->base, bar0->length) ||
        ranges_overlap(out_frts_result, sizeof(*out_frts_result),
                       (const void *)staged_vbios->cpu_address,
                       staged_vbios->allocated_size) ||
        ranges_overlap(out_trace, sizeof(*out_trace),
                       (const void *)staged_vbios->cpu_address,
                       staged_vbios->allocated_size) ||
        ranges_overlap(out_booter_mailbox1, sizeof(*out_booter_mailbox1),
                       (const void *)staged_vbios->cpu_address,
                       staged_vbios->allocated_size) ||
        ranges_overlap(out_frts_result, sizeof(*out_frts_result),
                       libos_init_args->cpu_address,
                       libos_init_args->allocated_size) ||
        ranges_overlap(out_trace, sizeof(*out_trace),
                       libos_init_args->cpu_address,
                       libos_init_args->allocated_size) ||
        ranges_overlap(out_booter_mailbox1, sizeof(*out_booter_mailbox1),
                       libos_init_args->cpu_address,
                       libos_init_args->allocated_size) ||
        ranges_overlap(out_frts_result, sizeof(*out_frts_result),
                       shared_queue_mem->cpu_address,
                       shared_queue_mem->allocated_size) ||
        ranges_overlap(out_trace, sizeof(*out_trace),
                       shared_queue_mem->cpu_address,
                       shared_queue_mem->allocated_size) ||
        ranges_overlap(out_booter_mailbox1, sizeof(*out_booter_mailbox1),
                       shared_queue_mem->cpu_address,
                       shared_queue_mem->allocated_size))
        return NVX_GSP_FALCON_INVALID_ARGUMENT;

    memset(out_frts_result, 0, sizeof(*out_frts_result));
    *out_booter_mailbox1 = 0U;
    memset(out_trace, 0, sizeof(*out_trace));
    trace.current_phase = NVX_GSP_BOOT_PHASE_PRECHECK;

    if (!memory->acquired || !memory->booters_staged ||
        !memory->wpr_metadata_prepared ||
        !memory->booter_load_sync.initialized ||
        memory->booter_load_sync.owner != NVX_DMA_OWNER_CPU ||
        !memory->wpr_metadata_sync.initialized ||
        memory->wpr_metadata_sync.owner != NVX_DMA_OWNER_CPU) {
        trace.result = NVX_GSP_FALCON_WPR_METADATA_NOT_READY;
        trace.failed_phase = NVX_GSP_BOOT_PHASE_PRECHECK;
        *out_trace = trace;
        return trace.result;
    }

    status = nvx_gsp_ga10x_execute_fwsec_frts(
        staged_vbios, staged_vbios_sync, fwsec_info, fuse_version, bar0,
        frts_framebuffer_offset, timeout_ms, poll_interval_ms,
        delay_ms, context, &frts_result);
    if (status != NVX_GSP_FALCON_OK) {
        trace.result = status;
        trace.failed_phase = NVX_GSP_BOOT_PHASE_PRECHECK;
        *out_trace = trace;
        return status;
    }
    *out_frts_result = frts_result;

    trace.current_phase = NVX_GSP_BOOT_PHASE_PREPARE_RISCV;
    status = nvx_gsp_ga10x_prepare_riscv_after_frts(
        memory, libos_init_args, libos_init_args_sync, shared_queue_mem,
        shared_queue_sync, bar0, frts_framebuffer_offset, &frts_result,
        timeout_ms, poll_interval_ms, delay_ms, context);
    if (status != NVX_GSP_FALCON_OK) {
        trace.result = status;
        trace.failed_phase = NVX_GSP_BOOT_PHASE_PREPARE_RISCV;
        *out_trace = trace;
        return status;
    }
    trace.last_completed_phase = NVX_GSP_BOOT_PHASE_PREPARE_RISCV;

    status = nvx_gsp_ga10x_execute_booter_load_traced(
        memory, bar0, timeout_ms, poll_interval_ms, delay_ms, context,
        &mailbox1, &trace);
    if (status != NVX_GSP_FALCON_OK) {
        *out_booter_mailbox1 = mailbox1;
        *out_trace = trace;
        return status;
    }
    *out_booter_mailbox1 = mailbox1;

    trace.current_phase = NVX_GSP_BOOT_PHASE_WAIT_RISCV_ACTIVE;
    status = nvx_gsp_ga10x_wait_riscv_active(
        bar0, timeout_ms, poll_interval_ms, delay_ms, context);
    if (status != NVX_GSP_FALCON_OK) {
        trace.result = status;
        trace.failed_phase = NVX_GSP_BOOT_PHASE_WAIT_RISCV_ACTIVE;
        trace.riscv_active = 0U;
        *out_trace = trace;
        return status;
    }

    trace.result = NVX_GSP_FALCON_OK;
    trace.current_phase = NVX_GSP_BOOT_PHASE_COMPLETE;
    trace.last_completed_phase = NVX_GSP_BOOT_PHASE_COMPLETE;
    trace.failed_phase = NVX_GSP_BOOT_PHASE_NONE;
    trace.riscv_active = 1U;
    *out_trace = trace;
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

static void trace_recovery_result(nvx_gsp_boot_trace_t *trace,
                                  nvx_gsp_falcon_status_t trigger_status,
                                  nvx_gsp_falcon_status_t recovery_status);

static nvx_gsp_falcon_status_t execute_booter_load_internal(
    nvx_gsp_boot_memory_t *memory, const nvx_mmio_region_t *bar0,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context,
    uint32_t *out_mailbox1, nvx_gsp_boot_trace_t *trace)
{
    nvx_gsp_secure_transfer_plan_t plan;
    nvx_gsp_falcon_status_t status;
    uint32_t mailbox0 = 0U, mailbox1 = 0U;

    if (trace) trace->current_phase = NVX_GSP_BOOT_PHASE_PRECHECK;
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

    if (trace) trace->current_phase = NVX_GSP_BOOT_PHASE_RESET_SEC2;
    status = nvx_ga10x_falcon_reset(bar0, NVX_SEC2_FALCON_BASE,
        timeout_ms, poll_interval_ms, delay_ms, context);
    if (status != NVX_GSP_FALCON_OK)
        return status;
    if (trace) { trace->last_completed_phase = NVX_GSP_BOOT_PHASE_RESET_SEC2; trace->sec2_quiescence_confirmed = 1U; }

    if (trace) trace->current_phase = NVX_GSP_BOOT_PHASE_TRANSFER_BOOTER_TO_DEVICE;
    if (nvx_dma_sync_for_device(&memory->booter_load_sync, 0U,
                                memory->booter_load.size) != NVX_DMA_SYNC_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    if (trace) trace->last_completed_phase = NVX_GSP_BOOT_PHASE_TRANSFER_BOOTER_TO_DEVICE;

    if (trace) trace->current_phase = NVX_GSP_BOOT_PHASE_UPLOAD_IMEM;
    status = nvx_ga10x_falcon_dma_upload(
        bar0, NVX_SEC2_FALCON_BASE, plan.source_phys,
        plan.imem_source_offset, plan.imem_destination_offset,
        plan.imem_size, NVX_GA10X_FALCON_SECURE_IMEM, 0U,
        timeout_ms, poll_interval_ms, delay_ms, context);
    if (status != NVX_GSP_FALCON_OK) {
        nvx_gsp_falcon_status_t reclaim = reset_and_reclaim(
            memory, bar0, timeout_ms, poll_interval_ms, delay_ms, context, 1U, 0U);
        trace_recovery_result(trace, status, reclaim);
        return reclaim == NVX_GSP_FALCON_OK ? status : reclaim;
    }

    if (trace) trace->last_completed_phase = NVX_GSP_BOOT_PHASE_UPLOAD_IMEM;
    if (trace) trace->current_phase = NVX_GSP_BOOT_PHASE_UPLOAD_DMEM;
    status = nvx_ga10x_falcon_dma_upload(
        bar0, NVX_SEC2_FALCON_BASE, plan.source_phys,
        plan.dmem_source_offset, plan.dmem_destination_offset,
        plan.dmem_size, NVX_GA10X_FALCON_DMEM, 0U,
        timeout_ms, poll_interval_ms, delay_ms, context);
    if (status != NVX_GSP_FALCON_OK) {
        nvx_gsp_falcon_status_t reclaim = reset_and_reclaim(
            memory, bar0, timeout_ms, poll_interval_ms, delay_ms, context, 1U, 0U);
        trace_recovery_result(trace, status, reclaim);
        return reclaim == NVX_GSP_FALCON_OK ? status : reclaim;
    }

    if (trace) trace->last_completed_phase = NVX_GSP_BOOT_PHASE_UPLOAD_DMEM;
    if (trace) trace->current_phase = NVX_GSP_BOOT_PHASE_RECLAIM_BOOTER;
    if (nvx_dma_sync_for_cpu(&memory->booter_load_sync, 0U,
                             memory->booter_load.size) != NVX_DMA_SYNC_OK) {
        nvx_gsp_falcon_status_t reclaim = reset_and_reclaim(
            memory, bar0, timeout_ms, poll_interval_ms, delay_ms, context, 1U, 0U);
        trace_recovery_result(trace, NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR,
                              reclaim);
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    }

    if (trace) trace->last_completed_phase = NVX_GSP_BOOT_PHASE_RECLAIM_BOOTER;
    if (trace) trace->current_phase = NVX_GSP_BOOT_PHASE_PROGRAM_BROM;
    status = nvx_ga10x_falcon_program_brom(
        bar0, NVX_SEC2_FALCON_BASE, NVX_SEC2_FALCON2_BASE,
        plan.engine_id_mask, plan.ucode_id, plan.signature_dmem_offset);
    if (status != NVX_GSP_FALCON_OK)
        return status;

    if (trace) trace->last_completed_phase = NVX_GSP_BOOT_PHASE_PROGRAM_BROM;
    if (trace) trace->current_phase = NVX_GSP_BOOT_PHASE_TRANSFER_WPR_TO_DEVICE;
    if (nvx_dma_sync_for_device(&memory->wpr_metadata_sync, 0U,
                                memory->wpr_metadata.size) != NVX_DMA_SYNC_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    if (trace) trace->last_completed_phase = NVX_GSP_BOOT_PHASE_TRANSFER_WPR_TO_DEVICE;

    if (trace) { trace->current_phase = NVX_GSP_BOOT_PHASE_START_SEC2; trace->sec2_quiescence_confirmed = 0U; }
    status = nvx_ga10x_falcon_boot(
        bar0, NVX_SEC2_FALCON_BASE, plan.boot_vector,
        1U, (uint32_t)memory->wpr_metadata.gpu_address,
        1U, (uint32_t)(memory->wpr_metadata.gpu_address >> 32U),
        &mailbox0, &mailbox1, timeout_ms, poll_interval_ms, delay_ms, context);
    if (status != NVX_GSP_FALCON_OK) {
        nvx_gsp_falcon_status_t reclaim = reset_and_reclaim(
            memory, bar0, timeout_ms, poll_interval_ms, delay_ms, context, 0U, 1U);
        trace_recovery_result(trace, status, reclaim);
        return reclaim == NVX_GSP_FALCON_OK ? status : reclaim;
    }

    if (trace) { trace->last_completed_phase = NVX_GSP_BOOT_PHASE_START_SEC2; trace->sec2_quiescence_confirmed = 1U; }
    if (trace) trace->current_phase = NVX_GSP_BOOT_PHASE_RECLAIM_WPR;
    if (nvx_dma_sync_for_cpu(&memory->wpr_metadata_sync, 0U,
                             memory->wpr_metadata.size) != NVX_DMA_SYNC_OK)
        return NVX_GSP_FALCON_DMA_OWNERSHIP_ERROR;
    if (trace) { trace->last_completed_phase = NVX_GSP_BOOT_PHASE_RECLAIM_WPR; trace->current_phase = NVX_GSP_BOOT_PHASE_VALIDATE_RESULT; }
    if (mailbox0 != 0U)
        return NVX_GSP_FALCON_BOOTER_REPORTED_ERROR;

    *out_mailbox1 = mailbox1;
    return NVX_GSP_FALCON_OK;
}

static void trace_recovery_result(nvx_gsp_boot_trace_t *trace,
                                  nvx_gsp_falcon_status_t trigger_status,
                                  nvx_gsp_falcon_status_t recovery_status)
{
    if (!trace) return;
    trace->recovery_trigger_status = trigger_status;
    trace->recovery_result = recovery_status;
    trace->recovery_attempted = 1U;
    trace->recovery_succeeded = recovery_status == NVX_GSP_FALCON_OK;
    trace->sec2_quiescence_confirmed = recovery_status == NVX_GSP_FALCON_OK;
}

nvx_gsp_falcon_status_t nvx_gsp_ga10x_execute_booter_load_traced(
    nvx_gsp_boot_memory_t *memory, const nvx_mmio_region_t *bar0,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context,
    uint32_t *out_mailbox1, nvx_gsp_boot_trace_t *out_trace)
{
    nvx_gsp_falcon_status_t status;
    if (out_trace) {
        if (ranges_overlap(out_trace, sizeof(*out_trace), memory, sizeof(*memory)) ||
            ranges_overlap(out_trace, sizeof(*out_trace), bar0, sizeof(*bar0)) ||
            ranges_overlap(out_trace, sizeof(*out_trace), out_mailbox1,
                           out_mailbox1 ? sizeof(*out_mailbox1) : 0U) ||
            (bar0 && bar0->base &&
             ranges_overlap(out_trace, sizeof(*out_trace),
                            (const void *)bar0->base, bar0->length)) ||
            (memory &&
             (ranges_overlap(out_trace, sizeof(*out_trace),
                             memory->booter_load.cpu_address,
                             memory->booter_load.allocated_size) ||
              ranges_overlap(out_trace, sizeof(*out_trace),
                             memory->wpr_metadata.cpu_address,
                             memory->wpr_metadata.allocated_size))))
            return NVX_GSP_FALCON_INVALID_ARGUMENT;
        memset(out_trace, 0, sizeof(*out_trace));
        out_trace->current_phase = NVX_GSP_BOOT_PHASE_PRECHECK;
        out_trace->failed_phase = NVX_GSP_BOOT_PHASE_PRECHECK;
        out_trace->result = NVX_GSP_FALCON_INVALID_ARGUMENT;
    }

    status = execute_booter_load_internal(memory, bar0, timeout_ms,
        poll_interval_ms, delay_ms, context, out_mailbox1, out_trace);
    if (out_trace) {
        out_trace->result = status;
        if (status == NVX_GSP_FALCON_OK) {
            out_trace->current_phase = NVX_GSP_BOOT_PHASE_COMPLETE;
            out_trace->last_completed_phase = NVX_GSP_BOOT_PHASE_COMPLETE;
            out_trace->failed_phase = NVX_GSP_BOOT_PHASE_NONE;
        } else {
            out_trace->failed_phase = out_trace->current_phase;
        }
    }
    return status;
}

nvx_gsp_falcon_status_t nvx_gsp_ga10x_execute_booter_load(
    nvx_gsp_boot_memory_t *memory, const nvx_mmio_region_t *bar0,
    uint32_t timeout_ms, uint32_t poll_interval_ms,
    nvx_gsp_delay_ms_fn delay_ms, void *context,
    uint32_t *out_mailbox1)
{
    return nvx_gsp_ga10x_execute_booter_load_traced(
        memory, bar0, timeout_ms, poll_interval_ms, delay_ms, context,
        out_mailbox1, NULL);
}
