/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 *
 * The wire layout is independently decoded from NVIDIA's public,
 * MIT-licensed kernel_gsp_frts_tu102.c interface definitions. This code does
 * not copy the NVIDIA or Nouveau implementation.
 */
#include "nvx_gsp_fwsec_interface.h"

#include <limits.h>
#include <string.h>

#define FWSEC_INTERFACE_VERSION 1U
#define FWSEC_INTERFACE_HEADER_SIZE 4U
#define FWSEC_INTERFACE_ENTRY_SIZE 8U
#define FWSEC_INTERFACE_MIN_ENTRIES 2U
#define FWSEC_INTERFACE_DMEM_MAPPER_ID 4U
/* The declared V3 fields total 64 bytes: 8-byte prefix + 14 u32 fields. */
#define FWSEC_DMEM_MAPPER_VERSION 3U
#define FWSEC_DMEM_MAPPER_V3_SIZE 64U

static int range_ok(size_t offset, size_t length, size_t limit)
{
    return offset <= limit && length <= limit - offset;
}

static int add_size(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX - b)
        return 0;
    *out = a + b;
    return 1;
}

static int offset_ranges_overlap(size_t a, size_t a_size,
                                 size_t b, size_t b_size)
{
    return a_size != 0U && b_size != 0U &&
           a < b + b_size && b < a + a_size;
}

static int overlaps(const void *a, size_t a_size,
                    const void *b, size_t b_size)
{
    uintptr_t x = (uintptr_t)a;
    uintptr_t y = (uintptr_t)b;

    if (a_size == 0U || b_size == 0U)
        return 0;
    if (x > UINTPTR_MAX - a_size || y > UINTPTR_MAX - b_size)
        return 1;
    return x < y + b_size && y < x + a_size;
}

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U));
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

nvx_gsp_fwsec_interface_status_t nvx_gsp_fwsec_find_command_window(
    const void *dmem, size_t dmem_size, uint32_t interface_offset,
    uint32_t command_id, size_t command_size,
    nvx_gsp_fwsec_command_window_t *out_window)
{
    const uint8_t *bytes = (const uint8_t *)dmem;
    nvx_gsp_fwsec_command_window_t result = {0};
    size_t entries_offset;
    size_t entries_bytes;
    size_t entries_end;
    size_t index;
    size_t mapper_offset = 0U;
    uint32_t mapper_count = 0U;
    uint8_t header_size;
    uint8_t entry_size;
    uint8_t entry_count;

    if (!dmem || !out_window || dmem_size == 0U ||
        dmem_size > UINT32_MAX || command_id == 0U ||
        command_size > UINT32_MAX)
        return NVX_GSP_FWSEC_INTERFACE_INVALID_ARGUMENT;
    if (overlaps(dmem, dmem_size, out_window, sizeof(*out_window)))
        return NVX_GSP_FWSEC_INTERFACE_OVERLAPPING_OUTPUT;
    memset(out_window, 0, sizeof(*out_window));

    if (!range_ok(interface_offset, FWSEC_INTERFACE_HEADER_SIZE, dmem_size))
        return NVX_GSP_FWSEC_INTERFACE_RANGE_ERROR;

    if (bytes[interface_offset] != FWSEC_INTERFACE_VERSION)
        return NVX_GSP_FWSEC_INTERFACE_BAD_HEADER;
    header_size = bytes[interface_offset + 1U];
    entry_size = bytes[interface_offset + 2U];
    entry_count = bytes[interface_offset + 3U];

    if (header_size < FWSEC_INTERFACE_HEADER_SIZE ||
        entry_size < FWSEC_INTERFACE_ENTRY_SIZE ||
        entry_count < FWSEC_INTERFACE_MIN_ENTRIES)
        return NVX_GSP_FWSEC_INTERFACE_BAD_HEADER;

    if (!add_size((size_t)interface_offset, header_size, &entries_offset) ||
        (size_t)entry_count > SIZE_MAX / (size_t)entry_size)
        return NVX_GSP_FWSEC_INTERFACE_RANGE_ERROR;
    entries_bytes = (size_t)entry_count * (size_t)entry_size;
    if (!range_ok(entries_offset, entries_bytes, dmem_size) ||
        !add_size(entries_offset, entries_bytes, &entries_end))
        return NVX_GSP_FWSEC_INTERFACE_RANGE_ERROR;
    (void)entries_end;

    for (index = 0U; index < entry_count; ++index) {
        size_t entry_offset = entries_offset + index * (size_t)entry_size;
        uint32_t entry_id = read_le32(bytes + entry_offset);
        uint32_t entry_dmem_offset = read_le32(bytes + entry_offset + 4U);

        if (entry_id != FWSEC_INTERFACE_DMEM_MAPPER_ID)
            continue;
        if (!range_ok(entry_dmem_offset, FWSEC_DMEM_MAPPER_V3_SIZE,
                      dmem_size))
            return NVX_GSP_FWSEC_INTERFACE_RANGE_ERROR;
        if (mapper_count != 0U)
            return NVX_GSP_FWSEC_INTERFACE_AMBIGUOUS;
        mapper_offset = entry_dmem_offset;
        mapper_count++;
    }

    if (mapper_count == 0U)
        return NVX_GSP_FWSEC_INTERFACE_NOT_FOUND;

    {
        const uint8_t *mapper = bytes + mapper_offset;
        uint16_t mapper_version = read_le16(mapper + 4U);
        uint16_t mapper_size = read_le16(mapper + 6U);
        uint32_t input_offset = read_le32(mapper + 8U);
        uint32_t input_capacity = read_le32(mapper + 12U);

        if (mapper_version != FWSEC_DMEM_MAPPER_VERSION ||
            mapper_size < FWSEC_DMEM_MAPPER_V3_SIZE ||
            !range_ok(mapper_offset, mapper_size, dmem_size))
            return NVX_GSP_FWSEC_INTERFACE_BAD_MAPPER;
        if (input_capacity == 0U ||
            !range_ok(input_offset, input_capacity, dmem_size))
            return NVX_GSP_FWSEC_INTERFACE_RANGE_ERROR;
        /* Command writes must not destroy the mapper or its own index table. */
        if (offset_ranges_overlap(input_offset, input_capacity,
                                  mapper_offset, mapper_size) ||
            offset_ranges_overlap(input_offset, input_capacity,
                                  interface_offset, entries_end - interface_offset))
            return NVX_GSP_FWSEC_INTERFACE_BAD_MAPPER;
        if (command_size > input_capacity)
            return NVX_GSP_FWSEC_INTERFACE_COMMAND_TOO_LARGE;

        result.mapper_offset = (uint32_t)mapper_offset;
        result.mapper_size = mapper_size;
        /* V3 mapper layout: init_cmd is the 12th 32-bit field, byte offset 44. */
        result.init_cmd_offset = (uint32_t)mapper_offset + 44U;
        result.command_input_offset = input_offset;
        result.command_input_capacity = input_capacity;
        result.command_id = command_id;
    }

    *out_window = result;
    return NVX_GSP_FWSEC_INTERFACE_OK;
}
