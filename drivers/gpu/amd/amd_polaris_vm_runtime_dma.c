/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_vm_runtime.h"
#include "gpu_dma_page_table.h"

amd_polaris_vm_runtime_status_t amd_polaris_vm_runtime_configure_dma(
    amd_polaris_vm_runtime_t *runtime, const gpu_mmio_region_t *mmio,
    uint64_t fault_page_physical, unsigned timeout_iterations)
{
    return amd_polaris_vm_runtime_configure(
        runtime, 0, gpu_dma_page_table_allocate, gpu_dma_page_table_free,
        mmio, fault_page_physical, timeout_iterations);
}
