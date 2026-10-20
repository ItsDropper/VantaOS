/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_boot_memory.h"
#include "nvx_gsp_elf.h"
#include "nvx_dma_sync.h"
#include <string.h>

static int ranges_overlap(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    if (!an || !bn) return 0;
    if (x > UINTPTR_MAX - an || y > UINTPTR_MAX - bn) return 1;
    return x < y + bn && y < x + an;
}

static void clear_memory(nvx_gsp_boot_memory_t *m)
{
    memset(m, 0, sizeof(*m));
}

void nvx_gsp_boot_memory_release(nvx_gsp_boot_memory_t *m)
{
    if (!m)
        return;
    if ((m->booter_load_sync.initialized && m->booter_load_sync.owner != NVX_DMA_OWNER_CPU) ||
        (m->booter_unload_sync.initialized && m->booter_unload_sync.owner != NVX_DMA_OWNER_CPU) ||
        (m->wpr_metadata_sync.initialized && m->wpr_metadata_sync.owner != NVX_DMA_OWNER_CPU))
        return; /* Never free DMA memory while the device may still own it. */
    if (m->booter_load_sync.initialized &&
        nvx_dma_sync_destroy(&m->booter_load_sync) != NVX_DMA_SYNC_OK) return;
    if (m->booter_unload_sync.initialized &&
        nvx_dma_sync_destroy(&m->booter_unload_sync) != NVX_DMA_SYNC_OK) return;
    if (m->wpr_metadata_sync.initialized &&
        nvx_dma_sync_destroy(&m->wpr_metadata_sync) != NVX_DMA_SYNC_OK) return;
    nvx_dma_release(&m->wpr_metadata);
    nvx_dma_release(&m->booter_unload);
    nvx_dma_release(&m->booter_load);
    nvx_dma_release(&m->signature);
    nvx_dma_release(&m->bootloader_payload);
    nvx_dma_release(&m->radix3_elf);
    clear_memory(m);
}

static int physical_ranges_overlap(const nvx_dma_buffer_t *a,
                                   const nvx_dma_buffer_t *b)
{
    uint64_t a_end, b_end;
    if (!a->acquired || !b->acquired || !a->allocated_size ||
        !b->allocated_size)
        return 0;
    if (a->gpu_address > UINT64_MAX - a->allocated_size ||
        b->gpu_address > UINT64_MAX - b->allocated_size)
        return 1;
    a_end = a->gpu_address + a->allocated_size;
    b_end = b->gpu_address + b->allocated_size;
    return a->gpu_address < b_end && b->gpu_address < a_end;
}

nvx_gsp_boot_memory_status_t nvx_gsp_boot_memory_acquire(
    const nvx_gsp_bundle_t *bundle, nvx_gsp_boot_memory_t *out)
{
    nvx_gsp_preflight_report_t report;
    nvx_gsp_elf_section_t signature;
    nvx_gsp_bootloader_info_t bootloader_info;
    nvx_gsp_preflight_status_t preflight;
    nvx_gsp_elf_status_t elf_status;
    nvx_gsp_bootloader_status_t bootloader_status;
    nvx_gsp_boot_memory_t memory = {0};
    const nvx_gsp_component_t *runtime;
    const nvx_gsp_component_t *bootloader;

    if (!bundle || !out)
        return NVX_GSP_BOOT_MEMORY_INVALID_ARGUMENT;
    if (out->acquired || out->radix3_elf.acquired ||
        out->bootloader_payload.acquired || out->signature.acquired ||
        out->booter_load.acquired || out->booter_unload.acquired ||
        out->wpr_metadata.acquired)
        return NVX_GSP_BOOT_MEMORY_INVALID_ARGUMENT;
    clear_memory(out);

    preflight = nvx_gsp_preflight_validate(bundle, &report);
    if (preflight != NVX_GSP_PREFLIGHT_OK)
        return NVX_GSP_BOOT_MEMORY_PREFLIGHT_FAILED;

    runtime = &bundle->components[0];
    bootloader = &bundle->components[4];
    elf_status = nvx_gsp_elf_find_section(runtime,
        NVX_GSP_GA10X_SIGNATURE_SECTION, &signature);
    if (elf_status != NVX_GSP_ELF_OK || !signature.data ||
        signature.size == 0U || signature.size > SIZE_MAX)
        return NVX_GSP_BOOT_MEMORY_ELF_SECTION_FAILED;

    bootloader_status = nvx_gsp_bootloader_parse(bootloader, &bootloader_info);
    if (bootloader_status != NVX_GSP_BOOTLOADER_OK ||
        bootloader_info.payload_size == 0U)
        return NVX_GSP_BOOT_MEMORY_PREFLIGHT_FAILED;

    if (report.booter_load.data_size == 0U || report.booter_unload.data_size == 0U ||
        nvx_dma_alloc(runtime->blob.size, 64U, &memory.radix3_elf) != NVX_DMA_OK ||
        nvx_dma_alloc(bootloader_info.payload_size, 64U,
                      &memory.bootloader_payload) != NVX_DMA_OK ||
        nvx_dma_alloc((size_t)signature.size, 64U, &memory.signature) != NVX_DMA_OK ||
        nvx_dma_alloc(report.booter_load.data_size, 64U, &memory.booter_load) != NVX_DMA_OK ||
        nvx_dma_alloc(report.booter_unload.data_size, 64U, &memory.booter_unload) != NVX_DMA_OK ||
        nvx_dma_alloc(NVX_DMA_PAGE_SIZE, 64U, &memory.wpr_metadata) != NVX_DMA_OK) {
        nvx_gsp_boot_memory_release(&memory);
        return NVX_GSP_BOOT_MEMORY_ALLOCATION_FAILED;
    }

    if (physical_ranges_overlap(&memory.radix3_elf, &memory.bootloader_payload) ||
        physical_ranges_overlap(&memory.radix3_elf, &memory.signature) ||
        physical_ranges_overlap(&memory.radix3_elf, &memory.wpr_metadata) ||
        physical_ranges_overlap(&memory.radix3_elf, &memory.booter_load) ||
        physical_ranges_overlap(&memory.radix3_elf, &memory.booter_unload) ||
        physical_ranges_overlap(&memory.bootloader_payload, &memory.signature) ||
        physical_ranges_overlap(&memory.bootloader_payload, &memory.wpr_metadata) ||
        physical_ranges_overlap(&memory.bootloader_payload, &memory.booter_load) ||
        physical_ranges_overlap(&memory.bootloader_payload, &memory.booter_unload) ||
        physical_ranges_overlap(&memory.signature, &memory.wpr_metadata) ||
        physical_ranges_overlap(&memory.signature, &memory.booter_load) ||
        physical_ranges_overlap(&memory.signature, &memory.booter_unload) ||
        physical_ranges_overlap(&memory.booter_load, &memory.booter_unload) ||
        physical_ranges_overlap(&memory.booter_load, &memory.wpr_metadata) ||
        physical_ranges_overlap(&memory.booter_unload, &memory.wpr_metadata)) {
        nvx_gsp_boot_memory_release(&memory);
        return NVX_GSP_BOOT_MEMORY_ALLOCATION_FAILED;
    }

    if (nvx_dma_sync_init(&memory.booter_load_sync, &memory.booter_load) != NVX_DMA_SYNC_OK ||
        nvx_dma_sync_init(&memory.booter_unload_sync, &memory.booter_unload) != NVX_DMA_SYNC_OK ||
        nvx_dma_sync_init(&memory.wpr_metadata_sync, &memory.wpr_metadata) != NVX_DMA_SYNC_OK) {
        nvx_gsp_boot_memory_release(&memory);
        return NVX_GSP_BOOT_MEMORY_ALLOCATION_FAILED;
    }

    memcpy(memory.radix3_elf.cpu_address, runtime->blob.data,
           runtime->blob.size);
    memcpy(memory.signature.cpu_address, signature.data, (size_t)signature.size);

    bootloader_status = nvx_gsp_bootloader_prepare(
        bootloader, (uint8_t *)memory.bootloader_payload.cpu_address,
        memory.bootloader_payload.size, &bootloader_info);
    if (bootloader_status != NVX_GSP_BOOTLOADER_OK) {
        nvx_gsp_boot_memory_release(&memory);
        return NVX_GSP_BOOT_MEMORY_COPY_FAILED;
    }

    memory.monitor_code_offset = bootloader_info.monitor_code_offset;
    memory.monitor_data_offset = bootloader_info.monitor_data_offset;
    memory.manifest_offset = bootloader_info.manifest_offset;
    memory.monitor_code_size = bootloader_info.monitor_code_size;
    memory.monitor_data_size = bootloader_info.monitor_data_size;
    memory.manifest_size = bootloader_info.manifest_size;
    memory.acquired = 1U;
    *out = memory;
    return NVX_GSP_BOOT_MEMORY_OK;
}

nvx_gsp_boot_memory_status_t nvx_gsp_boot_memory_bind_wpr(
    const nvx_gsp_boot_memory_t *memory, nvx_gsp_wpr_layout_t *layout)
{
    if (!memory || !layout || !memory->acquired ||
        !memory->radix3_elf.acquired ||
        !memory->bootloader_payload.acquired || !memory->signature.acquired ||
        !memory->booter_load.acquired || !memory->booter_unload.acquired ||
        !memory->wpr_metadata.acquired)
        return NVX_GSP_BOOT_MEMORY_INVALID_ARGUMENT;
    if (ranges_overlap(memory, sizeof(*memory), layout, sizeof(*layout)) ||
        ranges_overlap(memory->radix3_elf.cpu_address,
                       memory->radix3_elf.allocated_size, layout, sizeof(*layout)) ||
        ranges_overlap(memory->bootloader_payload.cpu_address,
                       memory->bootloader_payload.allocated_size, layout, sizeof(*layout)) ||
        ranges_overlap(memory->signature.cpu_address,
                       memory->signature.allocated_size, layout, sizeof(*layout)) ||
        ranges_overlap(memory->wpr_metadata.cpu_address,
                       memory->wpr_metadata.allocated_size, layout, sizeof(*layout)) ||
        ranges_overlap(memory->booter_load.cpu_address,
                       memory->booter_load.allocated_size, layout, sizeof(*layout)) ||
        ranges_overlap(memory->booter_unload.cpu_address,
                       memory->booter_unload.allocated_size, layout, sizeof(*layout)))
        return NVX_GSP_BOOT_MEMORY_INVALID_ARGUMENT;
    if ((memory->radix3_elf.gpu_address & 15U) != 0U ||
        (memory->bootloader_payload.gpu_address & 15U) != 0U ||
        (memory->signature.gpu_address & 15U) != 0U ||
        memory->radix3_elf.size == 0U ||
        memory->bootloader_payload.size == 0U ||
        memory->signature.size == 0U)
        return NVX_GSP_BOOT_MEMORY_COPY_FAILED;

    /* Preserve framebuffer offsets supplied by the platform planner. */
    layout->sysmem_radix3_addr = memory->radix3_elf.gpu_address;
    layout->radix3_size = memory->radix3_elf.size;
    layout->sysmem_bootloader_addr = memory->bootloader_payload.gpu_address;
    layout->bootloader_size = memory->bootloader_payload.size;
    layout->sysmem_signature_addr = memory->signature.gpu_address;
    layout->signature_size = memory->signature.size;
    layout->bootloader_code_offset = memory->monitor_code_offset;
    layout->bootloader_data_offset = memory->monitor_data_offset;
    layout->bootloader_manifest_offset = memory->manifest_offset;
    return NVX_GSP_BOOT_MEMORY_OK;
}

nvx_gsp_boot_memory_status_t nvx_gsp_boot_memory_prepare_wpr_metadata(
    nvx_gsp_boot_memory_t *memory, const nvx_gsp_wpr_layout_t *planned_layout,
    uint64_t *out_metadata_phys)
{
    nvx_gsp_wpr_layout_t layout;
    nvx_gsp_wpr_status_t status;

    if (!memory || !planned_layout || !out_metadata_phys ||
        !memory->acquired || !memory->radix3_elf.acquired ||
        !memory->bootloader_payload.acquired || !memory->signature.acquired ||
        !memory->booter_load.acquired || !memory->booter_unload.acquired ||
        !memory->wpr_metadata.acquired ||
        ranges_overlap(memory, sizeof(*memory), planned_layout,
                       sizeof(*planned_layout)) ||
        ranges_overlap(memory, sizeof(*memory), out_metadata_phys,
                       sizeof(*out_metadata_phys)) ||
        ranges_overlap(planned_layout, sizeof(*planned_layout), out_metadata_phys,
                       sizeof(*out_metadata_phys)) ||
        ranges_overlap(memory->wpr_metadata.cpu_address,
                       memory->wpr_metadata.allocated_size, planned_layout,
                       sizeof(*planned_layout)) ||
        ranges_overlap(memory->wpr_metadata.cpu_address,
                       memory->wpr_metadata.allocated_size, out_metadata_phys,
                       sizeof(*out_metadata_phys)))
        return NVX_GSP_BOOT_MEMORY_INVALID_ARGUMENT;

    if (!memory->wpr_metadata_sync.initialized ||
        memory->wpr_metadata_sync.owner != NVX_DMA_OWNER_CPU ||
        memory->wpr_metadata.allocated_size < NVX_DMA_PAGE_SIZE ||
        memory->wpr_metadata.gpu_address == 0U ||
        (memory->wpr_metadata.gpu_address & (NVX_DMA_PAGE_SIZE - 1U)) != 0U)
        return NVX_GSP_BOOT_MEMORY_WPR_METADATA_FAILED;

    layout = *planned_layout;
    if (nvx_gsp_boot_memory_bind_wpr(memory, &layout) != NVX_GSP_BOOT_MEMORY_OK)
        return NVX_GSP_BOOT_MEMORY_WPR_METADATA_FAILED;

    memory->wpr_metadata_prepared = 0U;
    memset(memory->wpr_metadata.cpu_address, 0,
           memory->wpr_metadata.allocated_size);
    status = nvx_gsp_wpr_meta_materialize(&layout,
        memory->wpr_metadata.cpu_address, memory->wpr_metadata.allocated_size,
        memory->wpr_metadata.gpu_address);
    if (status != NVX_GSP_WPR_OK) {
        memset(memory->wpr_metadata.cpu_address, 0,
               memory->wpr_metadata.allocated_size);
        return NVX_GSP_BOOT_MEMORY_WPR_METADATA_FAILED;
    }

    memory->wpr_metadata_prepared = 1U;
    *out_metadata_phys = memory->wpr_metadata.gpu_address;
    return NVX_GSP_BOOT_MEMORY_OK;
}

nvx_gsp_boot_memory_status_t nvx_gsp_boot_memory_stage_booters_from_bar0(
    nvx_gsp_boot_memory_t *memory, const nvx_gsp_bundle_t *bundle,
    const nvx_mmio_region_t *bar0)
{
    nvx_gsp_preflight_status_t status;

    if (!memory || !bundle || !bar0 || !bar0->base ||
        !memory->acquired || memory->booters_staged ||
        !memory->booter_load.acquired || !memory->booter_unload.acquired ||
        !memory->booter_load_sync.initialized || !memory->booter_unload_sync.initialized ||
        memory->booter_load_sync.owner != NVX_DMA_OWNER_CPU ||
        memory->booter_unload_sync.owner != NVX_DMA_OWNER_CPU ||
        ranges_overlap(memory, sizeof(*memory), bundle, sizeof(*bundle)) ||
        ranges_overlap(memory, sizeof(*memory), bar0, sizeof(*bar0)) ||
        ranges_overlap(memory->booter_load.cpu_address,
                       memory->booter_load.allocated_size, bundle, sizeof(*bundle)) ||
        ranges_overlap(memory->booter_unload.cpu_address,
                       memory->booter_unload.allocated_size, bundle, sizeof(*bundle)))
        return NVX_GSP_BOOT_MEMORY_INVALID_ARGUMENT;

    memset(memory->booter_load.cpu_address, 0,
           memory->booter_load.allocated_size);
    memset(memory->booter_unload.cpu_address, 0,
           memory->booter_unload.allocated_size);
    memset(&memory->booter_load_info, 0, sizeof(memory->booter_load_info));
    memset(&memory->booter_unload_info, 0, sizeof(memory->booter_unload_info));

    status = nvx_gsp_preflight_stage_booters_from_bar0(
        bundle, bar0,
        (uint8_t *)memory->booter_load.cpu_address, memory->booter_load.size,
        (uint8_t *)memory->booter_unload.cpu_address, memory->booter_unload.size,
        &memory->booter_load_info, &memory->booter_unload_info);
    if (status != NVX_GSP_PREFLIGHT_OK) {
        memset(memory->booter_load.cpu_address, 0,
               memory->booter_load.allocated_size);
        memset(memory->booter_unload.cpu_address, 0,
               memory->booter_unload.allocated_size);
        memset(&memory->booter_load_info, 0, sizeof(memory->booter_load_info));
        memset(&memory->booter_unload_info, 0, sizeof(memory->booter_unload_info));
        return NVX_GSP_BOOT_MEMORY_BOOTER_STAGE_FAILED;
    }

    memory->booters_staged = 1U;
    return NVX_GSP_BOOT_MEMORY_OK;
}

