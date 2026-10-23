/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_AMD_POLARIS_VM_RUNTIME_H
#define VANTA_AMD_POLARIS_VM_RUNTIME_H

#include "amd_polaris_vm.h"
#include "amd_polaris_vm_hw.h"
#include "gpu_page_table_vm.h"

typedef enum amd_polaris_vm_runtime_status {
    AMD_POLARIS_VM_RUNTIME_OK = 0,
    AMD_POLARIS_VM_RUNTIME_INVALID_ARGUMENT,
    AMD_POLARIS_VM_RUNTIME_DEVICE_BUSY,
    AMD_POLARIS_VM_RUNTIME_TABLE_INIT_FAILED,
    AMD_POLARIS_VM_RUNTIME_HARDWARE_INIT_FAILED,
    AMD_POLARIS_VM_RUNTIME_VM_ADAPTER_FAILED,
    AMD_POLARIS_VM_RUNTIME_HARDWARE_SHUTDOWN_FAILED,
    AMD_POLARIS_VM_RUNTIME_TABLE_DESTROY_FAILED
} amd_polaris_vm_runtime_status_t;

/*
 * Own this structure at a stable address for the entire page-table lifetime.
 * The allocator must provide DMA-coherent, physically contiguous regions:
 * 4 KiB child tables and a 32 MiB contiguous Polaris leaf table.
 */
typedef struct amd_polaris_vm_runtime {
    amd_polaris_vm_backend_t backend;
    void *allocator_context;
    int (*allocate_table)(void *context, size_t entry_count,
                          gpu_page_table_page_t *out_page);
    void (*free_page)(void *context, gpu_page_table_page_t *page);
    const gpu_mmio_region_t *mmio;
    uint64_t fault_page_physical;
    uint64_t root_physical;
    unsigned timeout_iterations;
} amd_polaris_vm_runtime_t;

/* Caller-supplied allocator variant for platforms with their own DMA policy. */
amd_polaris_vm_runtime_status_t amd_polaris_vm_runtime_configure(
    amd_polaris_vm_runtime_t *runtime, void *allocator_context,
    int (*allocate_table)(void *, size_t, gpu_page_table_page_t *),
    void (*free_page)(void *, gpu_page_table_page_t *),
    const gpu_mmio_region_t *mmio, uint64_t fault_page_physical,
    unsigned timeout_iterations);

/*
 * Native x86 path: uses the kernel's contiguous DMA allocator for root/child
 * tables and the required 32 MiB Polaris leaf table. DMA remains below 4 GiB
 * under the current PMM contract.
 */
amd_polaris_vm_runtime_status_t amd_polaris_vm_runtime_configure_dma(
    amd_polaris_vm_runtime_t *runtime, const gpu_mmio_region_t *mmio,
    uint64_t fault_page_physical, unsigned timeout_iterations);

amd_polaris_vm_runtime_status_t amd_polaris_vm_runtime_init_table(
    amd_polaris_vm_runtime_t *runtime, gpu_page_table_t *table,
    int device_quiesced);
amd_polaris_vm_runtime_status_t amd_polaris_vm_runtime_make_vm_ops(
    amd_polaris_vm_runtime_t *runtime, gpu_page_table_t *table,
    gpu_vm_ops_t *out_ops);
amd_polaris_vm_runtime_status_t amd_polaris_vm_runtime_shutdown(
    amd_polaris_vm_runtime_t *runtime, gpu_page_table_t *table,
    int device_quiesced);

#endif
