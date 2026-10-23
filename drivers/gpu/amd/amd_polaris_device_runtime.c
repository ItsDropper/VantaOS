/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_device_runtime.h"
#include "amd_polaris.h"

#include <string.h>

#define AMD_POLARIS_DEVICE_VM_TIMEOUT 100000U

static int cleanup_vm(amd_polaris_device_runtime_t *runtime)
{
    if (runtime->memory_initialized != 0U) {
        if (amd_polaris_memory_shutdown(&runtime->memory, 1) !=
            AMD_POLARIS_MEMORY_OK)
            return 0;
        runtime->memory_initialized = 0U;
    }
    if (runtime->vm_initialized != 0U) {
        if (amd_polaris_vm_runtime_shutdown(&runtime->vm_runtime,
                &runtime->page_table, 1) != AMD_POLARIS_VM_RUNTIME_OK)
            return 0;
        runtime->vm_initialized = 0U;
    }
    if (runtime->fault_page.acquired != 0U)
        gpu_dma_release(&runtime->fault_page);
    return 1;
}

static int cleanup_mapping(amd_polaris_device_runtime_t *runtime)
{
    if (!cleanup_vm(runtime))
        return 0;
    if (runtime->mapping.active != 0U &&
        gpu_pci_mmio_unmap(&runtime->mapping) != GPU_PCI_MMIO_OK)
        return 0;
    memset(runtime, 0, sizeof(*runtime));
    return 1;
}

amd_polaris_device_status_t amd_polaris_device_runtime_init(
    amd_polaris_device_runtime_t *runtime, const struct pci_device *device,
    size_t ring_bytes, int firmware_ready, int device_quiesced)
{
    gpu_pci_mmio_status_t map_status;
    amd_polaris_vm_runtime_status_t vm_status;
    amd_polaris_gfx_runtime_status_t gfx_status;

    if (runtime == NULL || device == NULL || runtime->initialized != 0U ||
        runtime->mapping.active != 0U || runtime->gfx.initialized != 0U ||
        runtime->vm_initialized != 0U || runtime->fault_page.acquired != 0U)
        return AMD_POLARIS_DEVICE_INVALID_ARGUMENT;
    if (!amd_polaris_match_id(device->vendor_id, device->device_id))
        return AMD_POLARIS_DEVICE_UNSUPPORTED;
    if (!device_quiesced || !firmware_ready)
        return AMD_POLARIS_DEVICE_NOT_READY;

    memset(runtime, 0, sizeof(*runtime));
    runtime->pci_device = device;

    /*
     * Polaris uses BAR5 for the register aperture; BAR2 is the doorbell
     * aperture and must not be treated as MMIO. BAR mapping requires bus
     * mastering to be disabled while resource/decode state is changed.
     */
    map_status = gpu_pci_mmio_map(device, AMD_POLARIS_MMIO_BAR,
        AMD_POLARIS_MMIO_BAR_SIZE, AMD_POLARIS_DEVICE_MMIO_BYTES,
        &runtime->mapping);
    if (map_status != GPU_PCI_MMIO_OK) {
        memset(runtime, 0, sizeof(*runtime));
        return AMD_POLARIS_DEVICE_MMIO_FAILED;
    }

    /*
     * Establish a real VMID-1 page-table root before any command ring can
     * reference GPU virtual addresses. The fault page remains pinned until
     * VMID 1 is disabled successfully.
     */
    if (gpu_dma_alloc(GPU_DMA_PAGE_SIZE, 32U, &runtime->fault_page) !=
        GPU_DMA_OK) {
        (void)cleanup_mapping(runtime);
        return AMD_POLARIS_DEVICE_DMA_FAILED;
    }
    memset(runtime->fault_page.cpu_address, 0, GPU_DMA_PAGE_SIZE);

    vm_status = amd_polaris_vm_runtime_configure_dma(&runtime->vm_runtime,
        &runtime->mapping.region, runtime->fault_page.gpu_address,
        AMD_POLARIS_DEVICE_VM_TIMEOUT);
    if (vm_status == AMD_POLARIS_VM_RUNTIME_OK)
        vm_status = amd_polaris_vm_runtime_init_table(&runtime->vm_runtime,
            &runtime->page_table, device_quiesced);
    if (vm_status != AMD_POLARIS_VM_RUNTIME_OK) {
        if (runtime->page_table.initialized != 0U ||
            runtime->vm_runtime.root_physical != 0U) {
            runtime->vm_initialized = 1U;
            runtime->initialized = 1U;
            return AMD_POLARIS_DEVICE_VM_FAILED;
        }
        (void)cleanup_mapping(runtime);
        return AMD_POLARIS_DEVICE_VM_FAILED;
    }
    runtime->vm_initialized = 1U;

    if (amd_polaris_vm_runtime_make_vm_ops(&runtime->vm_runtime,
            &runtime->page_table, &runtime->vm_ops) !=
        AMD_POLARIS_VM_RUNTIME_OK ||
        amd_polaris_memory_init(&runtime->memory, &runtime->vm_ops) !=
            AMD_POLARIS_MEMORY_OK) {
        if (!cleanup_mapping(runtime))
            runtime->initialized = 1U;
        return AMD_POLARIS_DEVICE_VM_FAILED;
    }
    runtime->memory_initialized = 1U;

    gfx_status = amd_polaris_gfx_runtime_init(&runtime->gfx, device,
        &runtime->mapping.region, ring_bytes, firmware_ready, device_quiesced);
    if (gfx_status != AMD_POLARIS_GFX_RUNTIME_OK) {
        /*
         * Retain the VM, fault page and BAR mapping if the command processor
         * could not be proven stopped or bus mastering could not be restored.
         */
        if (runtime->gfx.initialized != 0U ||
            runtime->gfx.ring.initialized != 0U) {
            runtime->initialized = 1U;
            return AMD_POLARIS_DEVICE_ENGINE_FAILED;
        }
        if (!cleanup_mapping(runtime))
            runtime->initialized = 1U;
        return AMD_POLARIS_DEVICE_ENGINE_FAILED;
    }

    runtime->initialized = 1U;
    return AMD_POLARIS_DEVICE_OK;
}

amd_polaris_device_status_t amd_polaris_device_submit_fence(
    amd_polaris_device_runtime_t *runtime, uint32_t *out_fence)
{
    if (runtime == NULL || runtime->initialized == 0U ||
        runtime->mapping.active == 0U || runtime->gfx.initialized == 0U)
        return AMD_POLARIS_DEVICE_INVALID_ARGUMENT;
    return amd_polaris_gfx_runtime_submit_fence(&runtime->gfx, out_fence) ==
        AMD_POLARIS_GFX_RUNTIME_OK ? AMD_POLARIS_DEVICE_OK :
                                     AMD_POLARIS_DEVICE_ENGINE_FAILED;
}

amd_polaris_device_status_t amd_polaris_device_wait_fence(
    amd_polaris_device_runtime_t *runtime, uint32_t fence,
    unsigned timeout_iterations)
{
    if (runtime == NULL || runtime->initialized == 0U ||
        runtime->mapping.active == 0U || runtime->gfx.initialized == 0U)
        return AMD_POLARIS_DEVICE_INVALID_ARGUMENT;
    return amd_polaris_gfx_runtime_wait_fence(
        &runtime->gfx, fence, timeout_iterations) ==
        AMD_POLARIS_GFX_RUNTIME_OK ? AMD_POLARIS_DEVICE_OK :
                                     AMD_POLARIS_DEVICE_ENGINE_FAILED;
}

amd_polaris_device_status_t amd_polaris_device_memory_alloc(
    amd_polaris_device_runtime_t *runtime, size_t size, uint64_t gpu_address,
    amd_polaris_gpu_allocation_t *out_allocation)
{
    if (runtime == NULL || runtime->initialized == 0U ||
        runtime->memory_initialized == 0U || runtime->vm_initialized == 0U)
        return AMD_POLARIS_DEVICE_NOT_READY;
    return amd_polaris_memory_alloc(&runtime->memory, size, gpu_address,
        out_allocation) == AMD_POLARIS_MEMORY_OK ?
        AMD_POLARIS_DEVICE_OK : AMD_POLARIS_DEVICE_VM_FAILED;
}

amd_polaris_device_status_t amd_polaris_device_memory_free(
    amd_polaris_gpu_allocation_t *allocation)
{
    return amd_polaris_memory_free(allocation) == AMD_POLARIS_MEMORY_OK ?
        AMD_POLARIS_DEVICE_OK : AMD_POLARIS_DEVICE_VM_FAILED;
}

amd_polaris_device_status_t amd_polaris_device_runtime_shutdown(
    amd_polaris_device_runtime_t *runtime, int device_quiesced)
{
    amd_polaris_gfx_runtime_status_t gfx_status;

    if (runtime == NULL || runtime->initialized == 0U ||
        runtime->pci_device == NULL)
        return AMD_POLARIS_DEVICE_INVALID_ARGUMENT;
    if (!device_quiesced)
        return AMD_POLARIS_DEVICE_NOT_READY;

    if (runtime->gfx.initialized != 0U) {
        gfx_status = amd_polaris_gfx_runtime_shutdown(&runtime->gfx, 1);
        if (gfx_status != AMD_POLARIS_GFX_RUNTIME_OK)
            return AMD_POLARIS_DEVICE_SHUTDOWN_FAILED;
    }
    if (!cleanup_mapping(runtime))
        return AMD_POLARIS_DEVICE_SHUTDOWN_FAILED;

    return AMD_POLARIS_DEVICE_OK;
}
