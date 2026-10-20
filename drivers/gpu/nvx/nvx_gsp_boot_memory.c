/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_boot_memory.h"
#include "nvx_gsp_elf.h"
#include <string.h>

static int ranges_overlap(const void *a, size_t an, const void *b, size_t bn)\n{\n    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;\n    if (!an || !bn) return 0;\n    if (x > UINTPTR_MAX - an || y > UINTPTR_MAX - bn) return 1;\n    return x < y + bn && y < x + an;\n}\n\nstatic void clear_memory(nvx_gsp_boot_memory_t *m)
{
    memset(m, 0, sizeof(*m));
}

void nvx_gsp_boot_memory_release(nvx_gsp_boot_memory_t *m)
{
    if (!m)
        return;
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
        out->bootloader_payload.acquired || out->signature.acquired)
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

    if (nvx_dma_alloc(runtime->blob.size, 64U, &memory.radix3_elf) != NVX_DMA_OK ||
        nvx_dma_alloc(bootloader_info.payload_size, 64U,
                      &memory.bootloader_payload) != NVX_DMA_OK ||
        nvx_dma_alloc((size_t)signature.size, 64U, &memory.signature) != NVX_DMA_OK) {
        nvx_gsp_boot_memory_release(&memory);
        return NVX_GSP_BOOT_MEMORY_ALLOCATION_FAILED;
    }

    if (physical_ranges_overlap(&memory.radix3_elf, &memory.bootloader_payload) ||
        physical_ranges_overlap(&memory.radix3_elf, &memory.signature) ||
        physical_ranges_overlap(&memory.bootloader_payload, &memory.signature)) {
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

    memory.acquired = 1U;
    *out = memory;
    return NVX_GSP_BOOT_MEMORY_OK;
}
