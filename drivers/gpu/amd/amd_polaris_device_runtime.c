/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_device_runtime.h"
#include "amd_polaris.h"

#include <string.h>

amd_polaris_device_status_t amd_polaris_device_runtime_init(
    amd_polaris_device_runtime_t *runtime, const struct pci_device *device,
    size_t ring_bytes, int firmware_ready, int device_quiesced)
{
    gpu_pci_mmio_status_t map_status;
    amd_polaris_gfx_runtime_status_t gfx_status;

    if (runtime == NULL || device == NULL || runtime->initialized != 0U ||
        runtime->mapping.active != 0U || runtime->gfx.initialized != 0U)
        return AMD_POLARIS_DEVICE_INVALID_ARGUMENT;
    if (!amd_polaris_match_id(device->vendor_id, device->device_id))
        return AMD_POLARIS_DEVICE_UNSUPPORTED;
    if (!device_quiesced || !firmware_ready)
        return AMD_POLARIS_DEVICE_NOT_READY;

    memset(runtime, 0, sizeof(*runtime));
    runtime->pci_device = device;

    /*
     * Size BAR2 while bus mastering is disabled, then map only the audited
     * register range used by the Polaris VM and GFX ring backends.
     */
    map_status = gpu_pci_mmio_map_sized(device, AMD_POLARIS_MMIO_BAR,
        AMD_POLARIS_DEVICE_MMIO_BYTES, &runtime->mapping);
    if (map_status != GPU_PCI_MMIO_OK) {
        memset(runtime, 0, sizeof(*runtime));
        return AMD_POLARIS_DEVICE_MMIO_FAILED;
    }

    gfx_status = amd_polaris_gfx_runtime_init(&runtime->gfx, device,
        &runtime->mapping.region, ring_bytes, firmware_ready, device_quiesced);
    if (gfx_status != AMD_POLARIS_GFX_RUNTIME_OK) {
        /*
         * The engine runtime can intentionally retain DMA when bus-master
         * disable cannot be verified. In that case keep the BAR mapping and
         * object alive so the caller can retry shutdown safely.
         */
        if (runtime->gfx.initialized != 0U) {
            runtime->initialized = 1U;
            return AMD_POLARIS_DEVICE_ENGINE_FAILED;
        }
        if (gpu_pci_mmio_unmap(&runtime->mapping) != GPU_PCI_MMIO_OK) {
            runtime->initialized = 1U;
            return AMD_POLARIS_DEVICE_MMIO_FAILED;
        }
        memset(runtime, 0, sizeof(*runtime));
        return AMD_POLARIS_DEVICE_ENGINE_FAILED;
    }

    runtime->initialized = 1U;
    return AMD_POLARIS_DEVICE_OK;
}

amd_polaris_device_status_t amd_polaris_device_submit_fence(
    amd_polaris_device_runtime_t *runtime, uint32_t *out_fence)
{
    if (runtime == NULL || runtime->initialized == 0U ||
        runtime->mapping.active == 0U)
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
        runtime->mapping.active == 0U)
        return AMD_POLARIS_DEVICE_INVALID_ARGUMENT;
    return amd_polaris_gfx_runtime_wait_fence(
        &runtime->gfx, fence, timeout_iterations) ==
        AMD_POLARIS_GFX_RUNTIME_OK ? AMD_POLARIS_DEVICE_OK :
                                     AMD_POLARIS_DEVICE_ENGINE_FAILED;
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
    if (runtime->mapping.active != 0U &&
        gpu_pci_mmio_unmap(&runtime->mapping) != GPU_PCI_MMIO_OK)
        return AMD_POLARIS_DEVICE_SHUTDOWN_FAILED;

    memset(runtime, 0, sizeof(*runtime));
    return AMD_POLARIS_DEVICE_OK;
}
