/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_gfx_runtime.h"

#include <string.h>

#define PCI_COMMAND_OFFSET 0x04U
#define PCI_COMMAND_BUS_MASTER 0x0004U
#define POLARIS_GFX_DEFAULT_RING_BYTES 65536U

static int power_of_two(size_t value)
{
    return value != 0U && (value & (value - 1U)) == 0U;
}

static void release_dma(amd_polaris_gfx_runtime_t *runtime)
{
    gpu_dma_release(&runtime->fence_dma);
    gpu_dma_release(&runtime->rptr_dma);
    gpu_dma_release(&runtime->ring_dma);
}

static int restore_bus_master(amd_polaris_gfx_runtime_t *runtime)
{
    uint16_t command;
    if (runtime->bus_master_enabled_by_us == 0U)
        return 1;

    command = pci_read_config16(runtime->pci_device, PCI_COMMAND_OFFSET);
    if (command == 0xFFFFU)
        return 0;
    command = (uint16_t)(command & (uint16_t)~PCI_COMMAND_BUS_MASTER);
    if (!pci_write_config16(runtime->pci_device, PCI_COMMAND_OFFSET, command))
        return 0;
    command = pci_read_config16(runtime->pci_device, PCI_COMMAND_OFFSET);
    if (command == 0xFFFFU ||
        (command & PCI_COMMAND_BUS_MASTER) != 0U)
        return 0;

    runtime->bus_master_enabled_by_us = 0U;
    return 1;
}

static int enable_bus_master(amd_polaris_gfx_runtime_t *runtime)
{
    uint16_t command;

    command = pci_read_config16(runtime->pci_device, PCI_COMMAND_OFFSET);
    if (command == 0xFFFFU)
        return 0;
    runtime->original_pci_command = command;
    if ((command & PCI_COMMAND_BUS_MASTER) != 0U)
        return 1;

    if (!pci_write_config16(runtime->pci_device, PCI_COMMAND_OFFSET,
            (uint16_t)(command | PCI_COMMAND_BUS_MASTER)))
        return 0;
    command = pci_read_config16(runtime->pci_device, PCI_COMMAND_OFFSET);
    if (command == 0xFFFFU ||
        (command & PCI_COMMAND_BUS_MASTER) == 0U) {
        (void)pci_write_config16(runtime->pci_device, PCI_COMMAND_OFFSET,
                                 runtime->original_pci_command);
        return 0;
    }
    runtime->bus_master_enabled_by_us = 1U;
    return 1;
}

static void cleanup_unstarted(amd_polaris_gfx_runtime_t *runtime)
{
    (void)restore_bus_master(runtime);
    release_dma(runtime);
    memset(runtime, 0, sizeof(*runtime));
}

amd_polaris_gfx_runtime_status_t amd_polaris_gfx_runtime_init(
    amd_polaris_gfx_runtime_t *runtime, const struct pci_device *pci_device,
    const gpu_mmio_region_t *mmio, size_t ring_bytes,
    int firmware_ready, int device_quiesced)
{
    amd_polaris_gfx_ring_status_t ring_status;

    if (runtime == NULL || pci_device == NULL || mmio == NULL ||
        mmio->base == NULL || runtime->initialized != 0U ||
        runtime->ring_dma.acquired != 0U ||
        runtime->rptr_dma.acquired != 0U ||
        runtime->fence_dma.acquired != 0U ||
        ring_bytes < AMD_POLARIS_GFX_RING_MIN_BYTES ||
        ring_bytes > AMD_POLARIS_GFX_RING_MAX_BYTES ||
        !power_of_two(ring_bytes))
        return AMD_POLARIS_GFX_RUNTIME_INVALID_ARGUMENT;
    if (!device_quiesced || !firmware_ready)
        return AMD_POLARIS_GFX_RUNTIME_NOT_READY;

    memset(runtime, 0, sizeof(*runtime));
    runtime->pci_device = pci_device;
    runtime->mmio = mmio;
    runtime->next_fence = 1U;

    /* Do not enable bus mastering until every device-visible buffer exists. */
    if (gpu_dma_alloc(ring_bytes, 32U, &runtime->ring_dma) != GPU_DMA_OK ||
        gpu_dma_alloc(GPU_DMA_PAGE_SIZE, 32U, &runtime->rptr_dma) != GPU_DMA_OK ||
        gpu_dma_alloc(GPU_DMA_PAGE_SIZE, 32U, &runtime->fence_dma) != GPU_DMA_OK) {
        release_dma(runtime);
        memset(runtime, 0, sizeof(*runtime));
        return AMD_POLARIS_GFX_RUNTIME_ALLOCATION_FAILED;
    }

    if (!gpu_dma_buffer_is_valid(&runtime->ring_dma) ||
        !gpu_dma_buffer_is_valid(&runtime->rptr_dma) ||
        !gpu_dma_buffer_is_valid(&runtime->fence_dma) ||
        runtime->ring_dma.gpu_address > ((1ULL << 40U) - ring_bytes) ||
        runtime->rptr_dma.gpu_address > ((1ULL << 40U) - GPU_DMA_PAGE_SIZE) ||
        runtime->fence_dma.gpu_address > ((1ULL << 40U) - GPU_DMA_PAGE_SIZE)) {
        release_dma(runtime);
        memset(runtime, 0, sizeof(*runtime));
        return AMD_POLARIS_GFX_RUNTIME_ALLOCATION_FAILED;
    }

    if (!enable_bus_master(runtime)) {
        cleanup_unstarted(runtime);
        return AMD_POLARIS_GFX_RUNTIME_PCI_ERROR;
    }

    ring_status = amd_polaris_gfx_ring_init(
        &runtime->ring, mmio, (volatile uint32_t *)runtime->ring_dma.cpu_address,
        runtime->ring_dma.gpu_address, ring_bytes, runtime->rptr_dma.gpu_address,
        firmware_ready, device_quiesced);
    if (ring_status != AMD_POLARIS_GFX_RING_OK) {
        if (runtime->ring.initialized != 0U) {
            runtime->initialized = 1U;
            runtime->faulted = 1U;
            /* Keep all DMA if the CP cannot be proven stopped. */
            if (amd_polaris_gfx_ring_shutdown(&runtime->ring,
                                               device_quiesced) !=
                AMD_POLARIS_GFX_RING_OK)
                return AMD_POLARIS_GFX_RUNTIME_RING_ERROR;
        }
        cleanup_unstarted(runtime);
        return AMD_POLARIS_GFX_RUNTIME_RING_ERROR;
    }

    runtime->initialized = 1U;
    return AMD_POLARIS_GFX_RUNTIME_OK;
}

amd_polaris_gfx_runtime_status_t amd_polaris_gfx_runtime_submit_fence(
    amd_polaris_gfx_runtime_t *runtime, uint32_t *out_fence_value)
{
    uint32_t packet[AMD_POLARIS_PM4_FENCE_DWORDS];
    uint32_t value;
    size_t packet_count = 0U;
    amd_polaris_pm4_status_t packet_status;
    amd_polaris_gfx_ring_status_t ring_status;

    if (runtime == NULL || out_fence_value == NULL ||
        runtime->initialized == 0U || runtime->ring.initialized == 0U)
        return AMD_POLARIS_GFX_RUNTIME_INVALID_ARGUMENT;
    *out_fence_value = 0U;
    if (runtime->faulted != 0U)
        return AMD_POLARIS_GFX_RUNTIME_NOT_READY;
    if (runtime->pending != 0U)
        return AMD_POLARIS_GFX_RUNTIME_BUSY;

    value = runtime->next_fence++;
    if (value == 0U) {
        value = runtime->next_fence++;
        if (value == 0U) {
            runtime->faulted = 1U;
            return AMD_POLARIS_GFX_RUNTIME_NOT_READY;
        }
    }

    *(volatile uint32_t *)runtime->fence_dma.cpu_address = 0U;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    packet_status = amd_polaris_build_fence_packet(packet,
        AMD_POLARIS_PM4_FENCE_DWORDS, runtime->fence_dma.gpu_address,
        value, &packet_count);
    if (packet_status != AMD_POLARIS_PM4_OK)
        return AMD_POLARIS_GFX_RUNTIME_RING_ERROR;

    ring_status = amd_polaris_gfx_ring_submit_fence(
        &runtime->ring, packet, packet_count, runtime->fence_dma.gpu_address,
        value);
    if (ring_status != AMD_POLARIS_GFX_RING_OK) {
        if (runtime->ring.faulted != 0U)
            runtime->faulted = 1U;
        return ring_status == AMD_POLARIS_GFX_RING_BUSY ?
            AMD_POLARIS_GFX_RUNTIME_BUSY : AMD_POLARIS_GFX_RUNTIME_RING_ERROR;
    }

    runtime->pending_fence = value;
    runtime->pending = 1U;
    *out_fence_value = value;
    return AMD_POLARIS_GFX_RUNTIME_OK;
}

amd_polaris_gfx_runtime_status_t amd_polaris_gfx_runtime_wait_fence(
    amd_polaris_gfx_runtime_t *runtime, uint32_t fence_value,
    unsigned timeout_iterations)
{
    amd_polaris_gfx_ring_status_t status;

    if (runtime == NULL || runtime->initialized == 0U ||
        runtime->ring.initialized == 0U || timeout_iterations == 0U)
        return AMD_POLARIS_GFX_RUNTIME_INVALID_ARGUMENT;
    if (runtime->faulted != 0U)
        return AMD_POLARIS_GFX_RUNTIME_NOT_READY;
    if (runtime->pending == 0U || fence_value == 0U ||
        fence_value != runtime->pending_fence)
        return AMD_POLARIS_GFX_RUNTIME_BUSY;

    status = amd_polaris_gfx_ring_wait_fence(
        &runtime->ring, (const volatile uint32_t *)runtime->fence_dma.cpu_address,
        fence_value, timeout_iterations);
    if (status != AMD_POLARIS_GFX_RING_OK) {
        runtime->faulted = 1U;
        return status == AMD_POLARIS_GFX_RING_FENCE_TIMEOUT ?
            AMD_POLARIS_GFX_RUNTIME_FENCE_TIMEOUT :
            AMD_POLARIS_GFX_RUNTIME_RING_ERROR;
    }

    runtime->pending = 0U;
    runtime->pending_fence = 0U;
    return AMD_POLARIS_GFX_RUNTIME_OK;
}

amd_polaris_gfx_runtime_status_t amd_polaris_gfx_runtime_shutdown(
    amd_polaris_gfx_runtime_t *runtime, int device_quiesced)
{
    if (runtime == NULL || runtime->initialized == 0U ||
        runtime->pci_device == NULL)
        return AMD_POLARIS_GFX_RUNTIME_INVALID_ARGUMENT;
    if (!device_quiesced)
        return AMD_POLARIS_GFX_RUNTIME_NOT_READY;

    if (runtime->ring.initialized != 0U &&
        amd_polaris_gfx_ring_shutdown(&runtime->ring, 1) !=
            AMD_POLARIS_GFX_RING_OK)
        return AMD_POLARIS_GFX_RUNTIME_SHUTDOWN_FAILED;

    if (!restore_bus_master(runtime))
        return AMD_POLARIS_GFX_RUNTIME_SHUTDOWN_FAILED;

    release_dma(runtime);
    memset(runtime, 0, sizeof(*runtime));
    return AMD_POLARIS_GFX_RUNTIME_OK;
}
