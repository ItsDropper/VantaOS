/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_elf_load.h"
#include <string.h>

static int ranges_overlap(uintptr_t a, size_t an, uintptr_t b, size_t bn)
{
    if (an == 0U || bn == 0U)
        return 0;
    if (a > UINTPTR_MAX - an || b > UINTPTR_MAX - bn)
        return 1;
    return a < b + bn && b < a + an;
}

static int physical_overlap(uint64_t a, uint64_t an, uint64_t b, uint64_t bn)
{
    if (an == 0U || bn == 0U)
        return 0;
    if (a > UINT64_MAX - an || b > UINT64_MAX - bn)
        return 1;
    return a < b + bn && b < a + an;
}

nvx_gsp_elf_load_status_t nvx_gsp_elf_load_segments(
    const nvx_gsp_component_t *component, nvx_gsp_elf_map_fn map,
    void *map_context, uint64_t *out_entry)
{
    nvx_gsp_elf_info_t info;
    uint8_t *destinations[NVX_GSP_ELF_MAX_LOAD_SEGMENTS] = {0};
    size_t lengths[NVX_GSP_ELF_MAX_LOAD_SEGMENTS] = {0};
    const uint8_t *image;
    size_t image_size;

    if (!component || !map || !out_entry || !component->blob.data ||
        !component->blob.size)
        return NVX_GSP_ELF_LOAD_INVALID_ARGUMENT;
    if (ranges_overlap((uintptr_t)out_entry, sizeof(*out_entry),
                       (uintptr_t)component, sizeof(*component)) ||
        ranges_overlap((uintptr_t)out_entry, sizeof(*out_entry),
                       (uintptr_t)component->blob.data, component->blob.size))
        return NVX_GSP_ELF_LOAD_INVALID_ARGUMENT;
    *out_entry = 0U;

    if (nvx_gsp_elf_parse(component, NVX_GSP_GA10X_SIGNATURE_SECTION, &info) !=
        NVX_GSP_ELF_OK)
        return NVX_GSP_ELF_LOAD_BAD_IMAGE;
    if (info.type != 2U)
        return NVX_GSP_ELF_LOAD_UNSUPPORTED_RELOCATION;

    image = component->blob.data;
    image_size = component->blob.size;
    for (uint32_t i = 0U; i < info.load_segment_count; ++i) {
        const nvx_gsp_elf_segment_t *seg = &info.load_segments[i];
        uint64_t end;
        if (seg->memory_size == 0U || seg->memory_size > SIZE_MAX ||
            seg->file_size > SIZE_MAX ||
            seg->file_offset > image_size ||
            seg->file_size > image_size - (size_t)seg->file_offset ||
            seg->physical_address > UINT64_MAX - seg->memory_size)
            return NVX_GSP_ELF_LOAD_ADDRESS_OVERFLOW;
        end = seg->physical_address + seg->memory_size;
        (void)end;

        for (uint32_t j = 0U; j < i; ++j) {
            const nvx_gsp_elf_segment_t *previous = &info.load_segments[j];
            if (physical_overlap(seg->physical_address, seg->memory_size,
                                 previous->physical_address, previous->memory_size))
                return NVX_GSP_ELF_LOAD_DESTINATION_OVERLAP;
        }

        if (!map(map_context, seg->physical_address, (size_t)seg->memory_size,
                 &destinations[i]) || !destinations[i])
            return NVX_GSP_ELF_LOAD_DESTINATION_UNAVAILABLE;
        lengths[i] = (size_t)seg->memory_size;

        if (ranges_overlap((uintptr_t)destinations[i], lengths[i],
                           (uintptr_t)image, image_size) ||
            ranges_overlap((uintptr_t)destinations[i], lengths[i],
                           (uintptr_t)component, sizeof(*component)) ||
            ranges_overlap((uintptr_t)destinations[i], lengths[i],
                           (uintptr_t)out_entry, sizeof(*out_entry)))
            return NVX_GSP_ELF_LOAD_DESTINATION_OVERLAP;
        for (uint32_t j = 0U; j < i; ++j) {
            if (ranges_overlap((uintptr_t)destinations[i], lengths[i],
                               (uintptr_t)destinations[j], lengths[j]))
                return NVX_GSP_ELF_LOAD_DESTINATION_OVERLAP;
        }
    }

    /* No writes occur until every segment has a valid, non-aliasing mapping. */
    for (uint32_t i = 0U; i < info.load_segment_count; ++i) {
        const nvx_gsp_elf_segment_t *seg = &info.load_segments[i];
        if (seg->file_size != 0U)
            memcpy(destinations[i], image + (size_t)seg->file_offset,
                   (size_t)seg->file_size);
        memset(destinations[i] + (size_t)seg->file_size, 0,
               lengths[i] - (size_t)seg->file_size);
    }

    *out_entry = info.entry;
    return NVX_GSP_ELF_LOAD_OK;
}
