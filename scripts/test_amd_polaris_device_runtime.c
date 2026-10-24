/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_device_runtime.h"
#include "heap.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MMIO_BYTES AMD_POLARIS_DEVICE_MMIO_BYTES
#define REG_VM_INVALIDATE_RESPONSE 0x147CU
#define REG_CP_RB_VMID (0x3051U * 4U)
#define PCI_COMMAND_BUS_MASTER 0x0004U

static uint32_t registers[MMIO_BYTES / sizeof(uint32_t)];
static uint8_t dma_pool[GPU_DMA_MAX_BYTES]
    __attribute__((aligned(GPU_DMA_PAGE_SIZE)));
static size_t dma_used;
static uint16_t pci_command = 0x0002U;
static unsigned maps_created;
static unsigned maps_destroyed;

int heap_is_initialized(void) { return 1; }
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr) { free(ptr); }

int amd_polaris_match_id(uint16_t vendor_id, uint16_t device_id)
{
    return vendor_id == 0x1002U &&
           (device_id == 0x67DFU || device_id == 0x67C0U);
}

uint16_t pci_read_config16(const struct pci_device *device, uint8_t offset)
{
    (void)device;
    return offset == 0x04U ? pci_command : 0xFFFFU;
}

int pci_write_config16(const struct pci_device *device, uint8_t offset,
                       uint16_t value)
{
    (void)device;
    if (offset != 0x04U)
        return 0;
    pci_command = value;
    return 1;
}

int dma_alloc_contiguous(dma_page_t *pages, size_t count)
{
    size_t bytes = count * GPU_DMA_PAGE_SIZE;
    if (pages == NULL || count == 0U || bytes > sizeof(dma_pool) - dma_used)
        return 0;
    memset(dma_pool + dma_used, 0, bytes);
    for (size_t i = 0U; i < count; ++i) {
        pages[i].virtual_address = dma_pool + dma_used +
                                   i * GPU_DMA_PAGE_SIZE;
        pages[i].physical_address = 0x20000000ULL + dma_used +
                                    i * GPU_DMA_PAGE_SIZE;
        pages[i].size = GPU_DMA_PAGE_SIZE;
    }
    dma_used += bytes;
    return 1;
}

int dma_page_is_valid(const dma_page_t *page)
{
    return page != NULL && page->virtual_address != NULL &&
        page->size == GPU_DMA_PAGE_SIZE &&
        (((uintptr_t)page->virtual_address & (GPU_DMA_PAGE_SIZE - 1U)) == 0U) &&
        ((page->physical_address & (GPU_DMA_PAGE_SIZE - 1U)) == 0U);
}

void dma_free_contiguous(dma_page_t *pages, size_t count)
{
    assert(pages != NULL && count != 0U && count <= GPU_DMA_MAX_PAGES);
    for (size_t i = 0U; i < count; ++i) {
        pages[i].virtual_address = NULL;
        pages[i].physical_address = 0U;
        pages[i].size = 0U;
    }
}

gpu_pci_mmio_status_t gpu_pci_mmio_map(
    const struct pci_device *device, uint8_t bar_index,
    size_t audited_bar_size, size_t map_length,
    gpu_pci_mmio_mapping_t *out_mapping)
{
    if (device == NULL || out_mapping == NULL || bar_index != 5U ||
        audited_bar_size < map_length || map_length > sizeof(registers) ||
        (pci_command & PCI_COMMAND_BUS_MASTER) != 0U)
        return GPU_PCI_MMIO_RESOURCE_INVALID;
    memset(out_mapping, 0, sizeof(*out_mapping));
    out_mapping->device = device;
    out_mapping->mapping = registers;
    out_mapping->region.base = (volatile uint8_t *)registers;
    out_mapping->region.length = map_length;
    out_mapping->mapped_length = map_length;
    out_mapping->bar_index = bar_index;
    out_mapping->active = 1U;
    ++maps_created;
    return GPU_PCI_MMIO_OK;
}

gpu_pci_mmio_status_t gpu_pci_mmio_unmap(gpu_pci_mmio_mapping_t *mapping)
{
    if (mapping == NULL || mapping->active == 0U ||
        mapping->mapping != registers)
        return GPU_PCI_MMIO_INVALID_ARGUMENT;
    memset(mapping, 0, sizeof(*mapping));
    ++maps_destroyed;
    return GPU_PCI_MMIO_OK;
}

int main(void)
{
    struct pci_device device = {
        .vendor_id = 0x1002U, .device_id = 0x67DFU,
        .class_code = 0x03U, .subclass = 0x00U
    };
    amd_polaris_device_runtime_t runtime = {0};
    amd_polaris_gpu_allocation_t extra = {0};
    uint32_t fence = 0U;

    memset(registers, 0, sizeof(registers));
    /* Model the hardware acknowledging VMID-1 invalidation requests. */
    registers[REG_VM_INVALIDATE_RESPONSE / sizeof(uint32_t)] = 1U << 1U;
    maps_created = 0U;
    maps_destroyed = 0U;

    assert(amd_polaris_device_runtime_init(&runtime, &device,
        AMD_POLARIS_GFX_RING_MIN_BYTES, 0, 1) ==
        AMD_POLARIS_DEVICE_NOT_READY);
    assert(runtime.mapping.active == 0U && dma_used == 0U);

    assert(amd_polaris_device_runtime_init(&runtime, &device,
        AMD_POLARIS_GFX_RING_MIN_BYTES, 1, 1) == AMD_POLARIS_DEVICE_OK);
    assert(runtime.initialized != 0U);
    assert(runtime.vm_initialized != 0U && runtime.memory_initialized != 0U);
    assert(runtime.page_table.initialized != 0U);
    assert(runtime.vm_runtime.root_physical != 0U);
    assert(runtime.gfx.ring.vmid == 1U);
    assert(registers[REG_CP_RB_VMID / sizeof(uint32_t)] == 1U);
    assert((pci_command & PCI_COMMAND_BUS_MASTER) != 0U);

    assert(amd_polaris_device_memory_alloc(&runtime, 8192U, 0U, &extra) ==
        AMD_POLARIS_DEVICE_OK);
    assert(extra.mapped != 0U && extra.page_count == 2U);

    /* Queue a real GFX8 WRITE_DATA into this allocation, followed by a fence. */
    assert(amd_polaris_device_write32(&runtime, &extra, 4U, 0xC0FFEE42U,
        &fence) == AMD_POLARIS_DEVICE_OK);
    assert(fence == 1U && runtime.gfx.pending != 0U);
    assert(runtime.gfx.ring.ring_storage[0] == AMD_POLARIS_PM4_PACKET3_WRITE_DATA);
    assert(runtime.gfx.ring.ring_storage[1] == AMD_POLARIS_PM4_WRITE_DATA_MEMORY);
    assert(runtime.gfx.ring.ring_storage[2] ==
        (uint32_t)(extra.gpu_address + 4U));
    assert(runtime.gfx.ring.ring_storage[3] ==
        (uint32_t)((extra.gpu_address + 4U) >> 32U));
    assert(runtime.gfx.ring.ring_storage[4] == 0xC0FFEE42U);
    assert(runtime.gfx.ring.ring_storage[5] == AMD_POLARIS_PM4_PACKET3_WRITE_DATA);
    assert(runtime.gfx.ring.ring_storage[9] == fence);

    /* Host regression emulates GPU execution; hardware is not available in CI. */
    *(volatile uint32_t *)((uint8_t *)extra.dma.cpu_address + 4U) = 0xC0FFEE42U;
    *(volatile uint32_t *)runtime.gfx.fence_allocation.dma.cpu_address = fence;
    assert(amd_polaris_device_wait_fence(&runtime, fence, 4U) ==
        AMD_POLARIS_DEVICE_OK);
    assert(*(uint32_t *)((uint8_t *)extra.dma.cpu_address + 4U) == 0xC0FFEE42U);
    assert(amd_polaris_device_memory_free(&extra) == AMD_POLARIS_DEVICE_OK);

    assert(amd_polaris_device_runtime_shutdown(&runtime, 0) ==
        AMD_POLARIS_DEVICE_NOT_READY);
    assert(amd_polaris_device_runtime_shutdown(&runtime, 1) ==
        AMD_POLARIS_DEVICE_OK);
    assert(runtime.initialized == 0U);
    assert((pci_command & PCI_COMMAND_BUS_MASTER) == 0U);
    assert(maps_created == 1U && maps_destroyed == 1U);
    return 0;
}
