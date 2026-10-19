/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_resource.h"
#include "mmio.h"

#include <assert.h>
#include <stdint.h>

static uint64_t mock_bar_base;
static int mock_bar_io;
static int mock_bar_64;
static uint64_t mock_bar_size;
static int mock_bar_present = 1;
static unsigned map_calls;
static unsigned unmap_calls;
static uint64_t mapped_base;
static size_t mapped_length;
static int map_should_fail;

int pci_get_bar(const struct pci_device *device, uint8_t index,
                uint64_t *address, int *is_io, int *is_64bit)
{
    if (device == 0 || index >= 6U || !mock_bar_present)
        return 0;
    *address = mock_bar_base;
    *is_io = mock_bar_io;
    *is_64bit = mock_bar_64;
    return 1;
}

int pci_get_bar_size(const struct pci_device *device, uint8_t index,
                     uint64_t *size)
{
    if (size != 0)
        *size = 0;
    if (device == 0 || index >= 6U || size == 0 || mock_bar_size == 0U)
        return 0;
    *size = mock_bar_size;
    return 1;
}

void *mmio_map_region(uint64_t physical_address, size_t length)
{
    ++map_calls;
    mapped_base = physical_address;
    mapped_length = length;
    return map_should_fail ? 0 : (void *)(uintptr_t)0x12345000U;
}

int mmio_unmap_region(void *virtual_address, size_t length)
{
    assert(virtual_address == (void *)(uintptr_t)0x12345000U);
    assert(length == mapped_length);
    ++unmap_calls;
    return 1;
}

static nvx_bar_resource_t valid_resource(void)
{
    nvx_bar_resource_t resource = {
        .base = 0x10000000ULL,
        .size = 0x1000ULL,
        .is_io = 0,
        .is_64bit = 1,
        .size_known = 1
    };
    return resource;
}

int main(void)
{
    struct pci_device device = { .vendor_id = 0x10DEU };
    nvx_bar_resource_t resource;
    void *mapping = (void *)(uintptr_t)1U;

    /* BAR discovery is read-only and does not invent a resource length. */
    mock_bar_base = 0x00000012ABC00000ULL;
    mock_bar_io = 0;
    mock_bar_64 = 1;
    assert(nvx_bar_resource_read(&device, 0U, &resource) == NVX_RESOURCE_OK);
    assert(resource.base == mock_bar_base);
    assert(resource.is_64bit == 1U);
    assert(resource.size == 0U && resource.size_known == 0U);
    assert(nvx_bar_resource_validate(&resource) == NVX_RESOURCE_SIZE_UNKNOWN);
    assert(nvx_bar_mmio_map(&resource, &mapping) == NVX_RESOURCE_SIZE_UNKNOWN);
    assert(mapping == 0 && map_calls == 0U);

    /* A quiescent PCI layer may provide an independently measured BAR size. */
    mock_bar_size = 0x1000ULL;
    assert(nvx_bar_resource_read(&device, 0U, &resource) == NVX_RESOURCE_OK);
    assert(resource.size_known == 1U && resource.size == 0x1000ULL);
    assert(nvx_bar_resource_validate(&resource) == NVX_RESOURCE_OK);
    mock_bar_size = 0U;

    assert(nvx_bar_resource_read(&device, 0U, 0) == NVX_RESOURCE_INVALID_ARGUMENT);
    assert(nvx_bar_resource_read(0, 0U, &resource) == NVX_RESOURCE_INVALID_ARGUMENT);
    assert(nvx_bar_resource_read(&device, 6U, &resource) == NVX_RESOURCE_INVALID_ARGUMENT);

    mock_bar_present = 0;
    assert(nvx_bar_resource_read(&device, 0U, &resource) == NVX_RESOURCE_ABSENT);
    mock_bar_present = 1;
    mock_bar_io = 1;
    assert(nvx_bar_resource_read(&device, 0U, &resource) == NVX_RESOURCE_IO_SPACE_UNSUPPORTED);
    mock_bar_io = 0;
    mock_bar_base = 0;
    assert(nvx_bar_resource_read(&device, 0U, &resource) == NVX_RESOURCE_INVALID_BASE);
    mock_bar_base = 0x10000008ULL;
    assert(nvx_bar_resource_read(&device, 0U, &resource) == NVX_RESOURCE_INVALID_BASE);
    mock_bar_base = 0x10000000ULL;
    mock_bar_64 = 2;
    assert(nvx_bar_resource_read(&device, 0U, &resource) == NVX_RESOURCE_INVALID_WIDTH);
    mock_bar_64 = 0;
    mock_bar_base = 0x100000000ULL;
    assert(nvx_bar_resource_read(&device, 0U, &resource) == NVX_RESOURCE_INVALID_WIDTH);

    resource = valid_resource();
    resource.is_io = 1;
    assert(nvx_bar_resource_validate(&resource) == NVX_RESOURCE_IO_SPACE_UNSUPPORTED);
    resource = valid_resource();
    resource.is_64bit = 2;
    assert(nvx_bar_resource_validate(&resource) == NVX_RESOURCE_INVALID_WIDTH);
    resource = valid_resource();
    resource.base = 0;
    assert(nvx_bar_resource_validate(&resource) == NVX_RESOURCE_INVALID_BASE);
    resource = valid_resource();
    resource.size_known = 0;
    assert(nvx_bar_resource_validate(&resource) == NVX_RESOURCE_SIZE_UNKNOWN);
    resource = valid_resource();
    resource.size = 0;
    assert(nvx_bar_resource_validate(&resource) == NVX_RESOURCE_INVALID_SIZE);
    resource = valid_resource();
    resource.size = 0x1800ULL;
    assert(nvx_bar_resource_validate(&resource) == NVX_RESOURCE_INVALID_SIZE);
    resource = valid_resource();
    resource.base = 0x10001000ULL;
    resource.size = 0x2000ULL;
    assert(nvx_bar_resource_validate(&resource) == NVX_RESOURCE_INVALID_SIZE);
    resource = valid_resource();
    resource.base = 0xFFFFFFFFFFFFFFF0ULL;
    resource.size = 0x20ULL;
    assert(nvx_bar_resource_validate(&resource) == NVX_RESOURCE_RANGE_OVERFLOW);

    resource = valid_resource();
    resource.is_64bit = 0;
    resource.base = 0x100000000ULL;
    assert(nvx_bar_resource_validate(&resource) == NVX_RESOURCE_INVALID_WIDTH);

    resource = valid_resource();
    assert(nvx_bar_mmio_map(&resource, 0) == NVX_RESOURCE_INVALID_ARGUMENT);
    assert(nvx_bar_mmio_map(&resource, &mapping) == NVX_RESOURCE_OK);
    assert(mapping == (void *)(uintptr_t)0x12345000U);
    assert(mapped_base == resource.base && mapped_length == resource.size);
    nvx_bar_mmio_unmap(&resource, mapping);
    assert(unmap_calls == 1U);

    map_should_fail = 1;
    mapping = (void *)(uintptr_t)1U;
    assert(nvx_bar_mmio_map(&resource, &mapping) == NVX_RESOURCE_MAPPING_FAILED);
    assert(mapping == 0);
    assert(map_calls == 2U && unmap_calls == 1U);

    /* An invalid resource must never be passed to the mapper or unmapper. */
    resource.size_known = 0;
    assert(nvx_bar_mmio_map(&resource, &mapping) == NVX_RESOURCE_SIZE_UNKNOWN);
    nvx_bar_mmio_unmap(&resource, (void *)(uintptr_t)0x12345000U);
    assert(map_calls == 2U && unmap_calls == 1U);
    return 0;
}
