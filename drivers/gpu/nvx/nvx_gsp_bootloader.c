/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_bootloader.h"

#include <string.h>

#define NVFW_HEADER_SIZE 24U

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static int range_ok(uint32_t total, uint32_t offset, uint32_t length)
{
    return offset <= total && length <= total - offset;
}

static int overlaps(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x = (uintptr_t)a;
    uintptr_t y = (uintptr_t)b;
    if (!an || !bn)
        return 0;
    if (x > UINTPTR_MAX - an || y > UINTPTR_MAX - bn)
        return 1;
    return x < y + bn && y < x + an;
}

nvx_gsp_bootloader_status_t nvx_gsp_bootloader_parse(
    const nvx_gsp_component_t *component, nvx_gsp_bootloader_info_t *out_info)
{
    nvx_gsp_bootloader_info_t info = {0};
    const uint8_t *data;
    uint32_t header_offset;
    uint64_t payload_end;
    uint64_t rounded_end;

    if (!component || !out_info)
        return NVX_GSP_BOOTLOADER_INVALID_ARGUMENT;
    if (overlaps(out_info, sizeof(*out_info), component, sizeof(*component)) ||
        (component->blob.data &&
         overlaps(out_info, sizeof(*out_info), component->blob.data,
                  component->blob.size)))
        return NVX_GSP_BOOTLOADER_INVALID_ARGUMENT;
    memset(out_info, 0, sizeof(*out_info));

    if (!component->acquired || component->kind != NVX_GSP_BOOTLOADER ||
        !component->blob.data)
        return NVX_GSP_BOOTLOADER_INVALID_ARGUMENT;
    if (component->blob.size < NVFW_HEADER_SIZE)
        return NVX_GSP_BOOTLOADER_RANGE_INVALID;

    data = component->blob.data;
    if (read_le32(data) != 0x10DEU)
        return NVX_GSP_BOOTLOADER_BAD_MAGIC;
    if (read_le32(data + 4U) != 1U)
        return NVX_GSP_BOOTLOADER_UNSUPPORTED_VERSION;

    info.binary_size = read_le32(data + 8U);
    header_offset = read_le32(data + 12U);
    info.payload_offset = read_le32(data + 16U);
    info.payload_size = read_le32(data + 20U);
    info.descriptor_offset = header_offset;
    if (info.binary_size < NVFW_HEADER_SIZE ||
        info.binary_size > component->blob.size ||
        (info.binary_size % NVX_GSP_BOOTLOADER_ALIGNMENT) != 0U ||
        header_offset != NVFW_HEADER_SIZE ||
        info.payload_offset != header_offset + NVX_GSP_BOOTLOADER_DESC_GA10X_SIZE)
        return NVX_GSP_BOOTLOADER_BAD_LAYOUT;

    info.descriptor_size = info.payload_offset - info.descriptor_offset;
    if (!range_ok(info.binary_size, info.payload_offset, info.payload_size))
        return NVX_GSP_BOOTLOADER_RANGE_INVALID;

    payload_end = (uint64_t)info.payload_offset + info.payload_size;
    rounded_end = (payload_end + NVX_GSP_BOOTLOADER_ALIGNMENT - 1U) &
                  ~((uint64_t)NVX_GSP_BOOTLOADER_ALIGNMENT - 1U);
    if (rounded_end != info.binary_size)
        return NVX_GSP_BOOTLOADER_BAD_LAYOUT;

    info.descriptor = data + info.descriptor_offset;
    *out_info = info;
    return NVX_GSP_BOOTLOADER_OK;
}

nvx_gsp_bootloader_status_t nvx_gsp_bootloader_prepare(
    const nvx_gsp_component_t *component, uint8_t *staging,
    size_t staging_capacity, nvx_gsp_bootloader_info_t *out_info)
{
    nvx_gsp_bootloader_info_t info;
    nvx_gsp_bootloader_status_t status;

    if (!component || !staging || !out_info)
        return NVX_GSP_BOOTLOADER_INVALID_ARGUMENT;
    status = nvx_gsp_bootloader_parse(component, &info);
    if (status != NVX_GSP_BOOTLOADER_OK)
        return status;
    if (staging_capacity < info.payload_size)
        return NVX_GSP_BOOTLOADER_BUFFER_TOO_SMALL;
    if (overlaps(staging, info.payload_size, component, sizeof(*component)) ||
        overlaps(staging, info.payload_size, component->blob.data,
                 component->blob.size) ||
        overlaps(out_info, sizeof(*out_info), staging, staging_capacity) ||
        overlaps(out_info, sizeof(*out_info), component, sizeof(*component)) ||
        overlaps(out_info, sizeof(*out_info), component->blob.data,
                 component->blob.size))
        return NVX_GSP_BOOTLOADER_INVALID_ARGUMENT;

    memcpy(staging, component->blob.data + info.payload_offset,
           info.payload_size);
    *out_info = info;
    return NVX_GSP_BOOTLOADER_OK;
}
