/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_gfx_runtime.h"
#include "heap.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MMIO_BYTES 0xD000U
#define REG_CP_RB0_RPTR (0x21C0U * 4U)
#define REG_CP_RB0_WPTR (0x3045U * 4U)
#define PCI_COMMAND_BUS_MASTER 0x0004U

static uint32_t registers[MMIO_BYTES / sizeof(uint32_t)];
static unsigned char dma_memory[GPU_DMA_MAX_BYTES]
    __attribute__((aligned(GPU_DMA_PAGE_SIZE)));
static size_t dma_used;
static uint16_t pci_command = 0x0002U;
static int reject_bus_master_clear;

int heap_is_initialized(void) { return 1; }
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr) { free(ptr); }

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
    if (reject_bus_master_clear &&
        (value & PCI_COMMAND_BUS_MASTER) == 0U)
        return 0;
    pci_command = value;
    return 1;
}

int dma_alloc_contiguous(dma_page_t *pages, size_t count)
{
    size_t bytes = count * GPU_DMA_PAGE_SIZE;
    if (pages == NULL || count == 0U || count > GPU_DMA_MAX_PAGES ||
        bytes > sizeof(dma_memory) - dma_used)
        return 0;
    memset(dma_memory + dma_used, 0, bytes);
    for (size_t i = 0U; i < count; ++i) {
        pages[i].virtual_address = dma_memory + dma_used +
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

int main(void)
{
    struct pci_device device = {0};
    gpu_mmio_region_t mmio = {
        .base = (volatile uint8_t *)registers,
        .length = sizeof(registers)
    };
    amd_polaris_gfx_runtime_t runtime = {0};
    uint32_t fence = 0U;

    memset(registers, 0, sizeof(registers));
    assert(amd_polaris_gfx_runtime_init(&runtime, &device, &mmio,
        AMD_POLARIS_GFX_RING_MIN_BYTES, 0, 1) ==
        AMD_POLARIS_GFX_RUNTIME_NOT_READY);
    assert(dma_used == 0U && pci_command == 0x0002U);

    assert(amd_polaris_gfx_runtime_init(&runtime, &device, &mmio,
        AMD_POLARIS_GFX_RING_MIN_BYTES, 1, 0) ==
        AMD_POLARIS_GFX_RUNTIME_NOT_READY);
    assert(dma_used == 0U && pci_command == 0x0002U);

    assert(amd_polaris_gfx_runtime_init(&runtime, &device, &mmio,
        AMD_POLARIS_GFX_RING_MIN_BYTES, 1, 1) ==
        AMD_POLARIS_GFX_RUNTIME_OK);
    assert(runtime.initialized != 0U && runtime.ring.initialized != 0U);
    assert((pci_command & PCI_COMMAND_BUS_MASTER) != 0U);
    assert(runtime.ring_dma.acquired && runtime.rptr_dma.acquired &&
           runtime.fence_dma.acquired);

    assert(amd_polaris_gfx_runtime_submit_fence(&runtime, &fence) ==
           AMD_POLARIS_GFX_RUNTIME_OK);
    assert(fence == 1U && runtime.pending != 0U);
    assert(registers[REG_CP_RB0_WPTR / 4U] == AMD_POLARIS_PM4_FENCE_DWORDS);
    *(volatile uint32_t *)runtime.fence_dma.cpu_address = fence;
    assert(amd_polaris_gfx_runtime_wait_fence(&runtime, fence, 2U) ==
           AMD_POLARIS_GFX_RUNTIME_OK);
    assert(runtime.pending == 0U);

    assert(amd_polaris_gfx_runtime_submit_fence(&runtime, &fence) ==
           AMD_POLARIS_GFX_RUNTIME_OK);
    assert(fence == 2U);
    assert(amd_polaris_gfx_runtime_wait_fence(&runtime, fence, 2U) ==
           AMD_POLARIS_GFX_RUNTIME_FENCE_TIMEOUT);
    assert(runtime.faulted != 0U);

    assert(amd_polaris_gfx_runtime_shutdown(&runtime, 0) ==
           AMD_POLARIS_GFX_RUNTIME_NOT_READY);
    reject_bus_master_clear = 1;
    assert(amd_polaris_gfx_runtime_shutdown(&runtime, 1) ==
           AMD_POLARIS_GFX_RUNTIME_SHUTDOWN_FAILED);
    assert(runtime.initialized != 0U);
    assert(runtime.ring_dma.acquired && runtime.rptr_dma.acquired &&
           runtime.fence_dma.acquired);
    reject_bus_master_clear = 0;
    assert(amd_polaris_gfx_runtime_shutdown(&runtime, 1) ==
           AMD_POLARIS_GFX_RUNTIME_OK);
    assert(runtime.initialized == 0U);
    assert((pci_command & PCI_COMMAND_BUS_MASTER) == 0U);
    assert((pci_command & 0x0002U) != 0U);
    return 0;
}
