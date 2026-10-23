/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_gen9_memory.h"
#include "intel_gen9.h"

#include <string.h>

static int acquire_render(void *context)
{
    intel_gen9_memory_runtime_t *runtime = context;
    return runtime != NULL && runtime->mapping.active != 0U &&
        intel_gen9_forcewake_render_acquire(&runtime->mapping.region,
            INTEL_GEN9_MEMORY_FORCEWAKE_TIMEOUT);
}

static int release_render(void *context)
{
    intel_gen9_memory_runtime_t *runtime = context;
    return runtime != NULL && runtime->mapping.active != 0U &&
        intel_gen9_forcewake_render_release(&runtime->mapping.region,
            INTEL_GEN9_MEMORY_FORCEWAKE_TIMEOUT);
}

static int overlaps(uint64_t a, size_t a_size, uint64_t b, size_t b_size)
{
    uint64_t a_end = a + (uint64_t)a_size;
    uint64_t b_end = b + (uint64_t)b_size;
    return a < b_end && b < a_end;
}

/* Return 1 for empty PTEs, 0 for occupied VA, -1 on MMIO failure. */
static int ggtt_range_empty(intel_gen9_memory_runtime_t *runtime,
                            uint64_t address, size_t size)
{
    size_t pages = size / GPU_DMA_PAGE_SIZE;
    for (size_t i = 0U; i < pages; ++i) {
        uint64_t pte = 0U;
        size_t offset = INTEL_GEN9_GGTT_PTE_BASE +
            (size_t)((address >> 12U) + i) * sizeof(uint64_t);
        if (gpu_mmio_read64(&runtime->mapping.region, offset, &pte) !=
            GPU_MMIO_OK) {
            runtime->faulted = 1U;
            return -1;
        }
        if (pte != 0U)
            return 0;
    }
    return 1;
}

static int address_available(intel_gen9_memory_runtime_t *runtime,
                             uint64_t address, size_t size)
{
    if (address < INTEL_GEN9_MEMORY_DEFAULT_GGTT_BASE ||
        address > INTEL_GEN9_MEMORY_DEFAULT_GGTT_BASE +
                      INTEL_GEN9_MEMORY_VA_SIZE ||
        (uint64_t)size >
            INTEL_GEN9_MEMORY_DEFAULT_GGTT_BASE +
                INTEL_GEN9_MEMORY_VA_SIZE - address)
        return 0;

    for (size_t i = 0U; i < INTEL_GEN9_MEMORY_MAX_ALLOCATIONS; ++i) {
        const intel_gen9_memory_slot_t *slot = &runtime->slots[i];
        if (slot->allocation != NULL &&
            overlaps(address, size, slot->address, slot->size))
            return 0;
    }
    return ggtt_range_empty(runtime, address, size) == 1;
}

static int find_slot(const intel_gen9_memory_runtime_t *runtime)
{
    for (size_t i = 0U; i < INTEL_GEN9_MEMORY_MAX_ALLOCATIONS; ++i)
        if (runtime->slots[i].allocation == NULL)
            return (int)i;
    return -1;
}

static int find_address(intel_gen9_memory_runtime_t *runtime,
                        size_t size, uint64_t *out_address)
{
    uint64_t candidate = INTEL_GEN9_MEMORY_DEFAULT_GGTT_BASE;
    const uint64_t limit = INTEL_GEN9_MEMORY_DEFAULT_GGTT_BASE +
                           INTEL_GEN9_MEMORY_VA_SIZE;

    while (candidate < limit && (uint64_t)size <= limit - candidate) {
        uint64_t next = candidate;
        int collision = 0;
        for (size_t i = 0U; i < INTEL_GEN9_MEMORY_MAX_ALLOCATIONS; ++i) {
            const intel_gen9_memory_slot_t *slot = &runtime->slots[i];
            if (slot->allocation != NULL &&
                overlaps(candidate, size, slot->address, slot->size)) {
                uint64_t end = slot->address + (uint64_t)slot->size;
                if (end > next)
                    next = end;
                collision = 1;
            }
        }
        if (!collision) {
            int empty = ggtt_range_empty(runtime, candidate, size);
            if (empty < 0)
                return 0;
            if (empty != 0) {
                *out_address = candidate;
                return 1;
            }
            candidate += GPU_DMA_PAGE_SIZE;
            continue;
        }
        if (next <= candidate)
            return 0;
        candidate = (next + GPU_DMA_PAGE_SIZE - 1U) &
                    ~((uint64_t)GPU_DMA_PAGE_SIZE - 1U);
    }
    return 0;
}

intel_gen9_memory_status_t intel_gen9_memory_init(
    intel_gen9_memory_runtime_t *runtime, const struct pci_device *device,
    int device_quiesced)
{
    if (runtime == NULL || device == NULL ||
        runtime->initialized != 0U || runtime->mapping.active != 0U)
        return INTEL_GEN9_MEMORY_INVALID_ARGUMENT;
    if (!device_quiesced)
        return INTEL_GEN9_MEMORY_NOT_READY;
    if (!intel_gen9_match_id(device->vendor_id, device->device_id))
        return INTEL_GEN9_MEMORY_UNSUPPORTED_DEVICE;

    memset(runtime, 0, sizeof(*runtime));
    runtime->device = device;
    if (gpu_pci_mmio_map(device, INTEL_GEN9_GTTMMADR_BAR,
            INTEL_GEN9_GTTMMADR_SIZE, INTEL_GEN9_GTTMMADR_SIZE,
            &runtime->mapping) != GPU_PCI_MMIO_OK) {
        memset(runtime, 0, sizeof(*runtime));
        return INTEL_GEN9_MEMORY_MMIO_FAILED;
    }

    runtime->forcewake.context = runtime;
    runtime->forcewake.acquire_render = acquire_render;
    runtime->forcewake.release_render = release_render;
    runtime->initialized = 1U;
    return INTEL_GEN9_MEMORY_OK;
}

intel_gen9_memory_status_t intel_gen9_memory_alloc(
    intel_gen9_memory_runtime_t *runtime, size_t size, uint64_t ggtt_address,
    intel_gen9_gpu_allocation_t *out_allocation)
{
    intel_gen9_gpu_allocation_t allocation = {0};
    intel_gen9_ggtt_status_t status;
    uint64_t address = ggtt_address;
    int slot;

    if (runtime == NULL || out_allocation == NULL ||
        runtime->initialized == 0U || runtime->mapping.active == 0U ||
        out_allocation->mapped != 0U || out_allocation->dma.acquired != 0U ||
        size == 0U || size > GPU_DMA_MAX_BYTES)
        return INTEL_GEN9_MEMORY_INVALID_ARGUMENT;
    if (runtime->faulted != 0U)
        return INTEL_GEN9_MEMORY_NOT_READY;

    slot = find_slot(runtime);
    if (slot < 0)
        return INTEL_GEN9_MEMORY_ADDRESS_BUSY;
    if (gpu_dma_alloc(size, 32U, &allocation.dma) != GPU_DMA_OK)
        return INTEL_GEN9_MEMORY_DMA_FAILED;

    if (address == 0U) {
        if (!find_address(runtime, allocation.dma.allocated_size, &address)) {
            gpu_dma_release(&allocation.dma);
            return runtime->faulted != 0U ? INTEL_GEN9_MEMORY_MMIO_FAILED :
                                            INTEL_GEN9_MEMORY_ADDRESS_BUSY;
        }
    } else if ((address & (GPU_DMA_PAGE_SIZE - 1U)) != 0U ||
               !address_available(runtime, address,
                                  allocation.dma.allocated_size)) {
        gpu_dma_release(&allocation.dma);
        return runtime->faulted != 0U ? INTEL_GEN9_MEMORY_MMIO_FAILED :
                                        INTEL_GEN9_MEMORY_ADDRESS_BUSY;
    }

    allocation.runtime = runtime;
    allocation.ggtt_address = address;
    status = intel_gen9_ggtt_map_contiguous(&runtime->mapping.region,
        address, allocation.dma.gpu_address, allocation.dma.allocated_size,
        &runtime->forcewake);
    if (status != INTEL_GEN9_GGTT_OK) {
        if (status == INTEL_GEN9_GGTT_STATE_UNCERTAIN) {
            allocation.mapped = 1U;
            *out_allocation = allocation;
            runtime->slots[slot].allocation = out_allocation;
            runtime->slots[slot].address = address;
            runtime->slots[slot].size = allocation.dma.allocated_size;
            runtime->faulted = 1U;
            return INTEL_GEN9_MEMORY_GGTT_FAILED;
        }
        gpu_dma_release(&allocation.dma);
        return status == INTEL_GEN9_GGTT_FORCEWAKE_FAILED ?
            INTEL_GEN9_MEMORY_FORCEWAKE_FAILED : INTEL_GEN9_MEMORY_GGTT_FAILED;
    }

    allocation.mapped = 1U;
    *out_allocation = allocation;
    runtime->slots[slot].allocation = out_allocation;
    runtime->slots[slot].address = address;
    runtime->slots[slot].size = allocation.dma.allocated_size;
    return INTEL_GEN9_MEMORY_OK;
}

intel_gen9_memory_status_t intel_gen9_memory_free(
    intel_gen9_gpu_allocation_t *allocation)
{
    intel_gen9_memory_runtime_t *runtime;
    intel_gen9_ggtt_status_t status;
    int slot = -1;

    if (allocation == NULL || allocation->mapped == 0U ||
        allocation->runtime == NULL || allocation->dma.acquired == 0U)
        return INTEL_GEN9_MEMORY_INVALID_ARGUMENT;
    runtime = allocation->runtime;
    if (runtime->initialized == 0U || runtime->mapping.active == 0U)
        return INTEL_GEN9_MEMORY_NOT_READY;

    for (size_t i = 0U; i < INTEL_GEN9_MEMORY_MAX_ALLOCATIONS; ++i) {
        if (runtime->slots[i].allocation == allocation) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0 || runtime->slots[slot].address != allocation->ggtt_address ||
        runtime->slots[slot].size != allocation->dma.allocated_size)
        return INTEL_GEN9_MEMORY_INVALID_ARGUMENT;

    status = intel_gen9_ggtt_unmap_contiguous(&runtime->mapping.region,
        allocation->ggtt_address, allocation->dma.allocated_size,
        &runtime->forcewake);
    if (status != INTEL_GEN9_GGTT_OK) {
        runtime->faulted = 1U;
        return INTEL_GEN9_MEMORY_GGTT_FAILED;
    }

    gpu_dma_release(&allocation->dma);
    memset(allocation, 0, sizeof(*allocation));
    memset(&runtime->slots[slot], 0, sizeof(runtime->slots[slot]));
    return INTEL_GEN9_MEMORY_OK;
}

intel_gen9_memory_status_t intel_gen9_memory_shutdown(
    intel_gen9_memory_runtime_t *runtime, int device_quiesced)
{
    if (runtime == NULL || runtime->initialized == 0U ||
        runtime->mapping.active == 0U)
        return INTEL_GEN9_MEMORY_INVALID_ARGUMENT;
    if (!device_quiesced)
        return INTEL_GEN9_MEMORY_NOT_READY;
    for (size_t i = 0U; i < INTEL_GEN9_MEMORY_MAX_ALLOCATIONS; ++i)
        if (runtime->slots[i].allocation != NULL)
            return INTEL_GEN9_MEMORY_ADDRESS_BUSY;

    if (gpu_pci_mmio_unmap(&runtime->mapping) != GPU_PCI_MMIO_OK)
        return INTEL_GEN9_MEMORY_SHUTDOWN_FAILED;
    memset(runtime, 0, sizeof(*runtime));
    return INTEL_GEN9_MEMORY_OK;
}
