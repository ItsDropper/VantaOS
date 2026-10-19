/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_resource.h"
#include "mmio.h"

#include <limits.h>
#include <stdint.h>

static void nvx_resource_clear(nvx_bar_resource_t *resource)
{
    resource->base = 0;
    resource->size = 0;
    resource->is_io = 0;
    resource->is_64bit = 0;
    resource->size_known = 0;
}

nvx_resource_status_t nvx_bar_resource_read(
    const struct pci_device *device, uint8_t index,
    nvx_bar_resource_t *out_resource)
{
    uint64_t base = 0;
    int is_io = 0;
    int is_64bit = 0;
    uint64_t size = 0;

    if (out_resource == 0)
        return NVX_RESOURCE_INVALID_ARGUMENT;

    nvx_resource_clear(out_resource);
    if (device == 0 || index >= 6U)
        return NVX_RESOURCE_INVALID_ARGUMENT;

    if (!pci_get_bar(device, index, &base, &is_io, &is_64bit))
        return NVX_RESOURCE_ABSENT;
    if (is_io != 0 && is_io != 1)
        return NVX_RESOURCE_INVALID_ARGUMENT;
    if (is_64bit != 0 && is_64bit != 1)
        return NVX_RESOURCE_INVALID_WIDTH;
    if (is_io)
        return NVX_RESOURCE_IO_SPACE_UNSUPPORTED;
    if (base == 0 || (base & 0xFU) != 0)
        return NVX_RESOURCE_INVALID_BASE;
    if (!is_64bit && base > UINT32_MAX)
        return NVX_RESOURCE_INVALID_WIDTH;

    out_resource->base = base;
    out_resource->is_io = 0;
    out_resource->is_64bit = (uint8_t)is_64bit;
    /* BAR sizing refuses active decode/DMA; failure remains SIZE_UNKNOWN. */
    if (pci_get_bar_size(device, index, &size) && size != 0U) {
        out_resource->size = size;
        out_resource->size_known = 1;
    }
    return NVX_RESOURCE_OK;
}

nvx_resource_status_t nvx_bar_resource_validate(
    const nvx_bar_resource_t *resource)
{
    uint64_t size;

    if (resource == 0)
        return NVX_RESOURCE_INVALID_ARGUMENT;
    if (resource->is_io)
        return NVX_RESOURCE_IO_SPACE_UNSUPPORTED;
    if (resource->is_64bit > 1U)
        return NVX_RESOURCE_INVALID_WIDTH;
    if (resource->base == 0 || (resource->base & 0xFU) != 0)
        return NVX_RESOURCE_INVALID_BASE;
    if (!resource->is_64bit && resource->base > UINT32_MAX)
        return NVX_RESOURCE_INVALID_WIDTH;
    if (!resource->size_known)
        return NVX_RESOURCE_SIZE_UNKNOWN;

    size = resource->size;
    if (size < 16U || (size & (size - 1U)) != 0)
        return NVX_RESOURCE_INVALID_SIZE;
    if (size - 1U > UINT64_MAX - resource->base)
        return NVX_RESOURCE_RANGE_OVERFLOW;
    if ((resource->base & (size - 1U)) != 0)
        return NVX_RESOURCE_INVALID_SIZE;
    #if SIZE_MAX < UINT64_MAX
    if (size > (uint64_t)SIZE_MAX)
        return NVX_RESOURCE_INVALID_SIZE;
    #endif
    return NVX_RESOURCE_OK;
}

nvx_resource_status_t nvx_bar_mmio_map(
    const nvx_bar_resource_t *resource, void **out_mapping)
{
    nvx_resource_status_t status;
    void *mapping;

    if (out_mapping == 0)
        return NVX_RESOURCE_INVALID_ARGUMENT;
    *out_mapping = 0;

    status = nvx_bar_resource_validate(resource);
    if (status != NVX_RESOURCE_OK)
        return status;

    mapping = mmio_map_region(resource->base, (size_t)resource->size);
    if (mapping == 0)
        return NVX_RESOURCE_MAPPING_FAILED;
    *out_mapping = mapping;
    return NVX_RESOURCE_OK;
}

void nvx_bar_mmio_unmap(const nvx_bar_resource_t *resource, void *mapping)
{
    if (mapping == 0 ||
        nvx_bar_resource_validate(resource) != NVX_RESOURCE_OK)
        return;
    (void)mmio_unmap_region(mapping, (size_t)resource->size);
}
