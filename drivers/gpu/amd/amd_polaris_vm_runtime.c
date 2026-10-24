/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_vm_runtime.h"

#include <string.h>

static int runtime_allocate_table(void *context, size_t entry_count,
                                  gpu_page_table_page_t *out_page)
{
    amd_polaris_vm_runtime_t *runtime = context;
    return runtime->allocate_table(runtime->allocator_context, entry_count,
                                   out_page);
}

static void runtime_free_page(void *context, gpu_page_table_page_t *page)
{
    amd_polaris_vm_runtime_t *runtime = context;
    runtime->free_page(runtime->allocator_context, page);
}

static int runtime_invalidate(void *context, uint64_t root_physical)
{
    amd_polaris_vm_runtime_t *runtime = context;
    if (root_physical != runtime->root_physical)
        return 0;
    return amd_polaris_vm_invalidate(runtime->mmio,
        runtime->timeout_iterations) == AMD_POLARIS_VM_HW_OK;
}

amd_polaris_vm_runtime_status_t amd_polaris_vm_runtime_configure(
    amd_polaris_vm_runtime_t *runtime, void *allocator_context,
    int (*allocate_table)(void *, size_t, gpu_page_table_page_t *),
    void (*free_page)(void *, gpu_page_table_page_t *),
    const gpu_mmio_region_t *mmio, uint64_t fault_page_physical,
    unsigned timeout_iterations)
{
    if (runtime == 0 || allocate_table == 0 || free_page == 0 ||
        mmio == 0 || mmio->base == 0 ||
        mmio->length < 0x1584U || timeout_iterations == 0U ||
        (fault_page_physical & 0xFFFU) != 0U ||
        fault_page_physical > 0xFFFFFFFFFFULL)
        return AMD_POLARIS_VM_RUNTIME_INVALID_ARGUMENT;

    memset(runtime, 0, sizeof(*runtime));
    runtime->allocator_context = allocator_context;
    runtime->allocate_table = allocate_table;
    runtime->free_page = free_page;
    runtime->mmio = mmio;
    runtime->fault_page_physical = fault_page_physical;
    runtime->timeout_iterations = timeout_iterations;

    runtime->backend.context = runtime;
    runtime->backend.allocate_table = runtime_allocate_table;
    runtime->backend.free_page = runtime_free_page;
    runtime->backend.invalidate_tlb = runtime_invalidate;
    return AMD_POLARIS_VM_RUNTIME_OK;
}

amd_polaris_vm_runtime_status_t amd_polaris_vm_runtime_init_table(
    amd_polaris_vm_runtime_t *runtime, gpu_page_table_t *table,
    int device_quiesced)
{
    gpu_page_table_ops_t ops;
    gpu_page_table_status_t table_status;

    if (runtime == 0 || table == 0 || runtime->mmio == 0 ||
        runtime->backend.context != runtime)
        return AMD_POLARIS_VM_RUNTIME_INVALID_ARGUMENT;
    if (!device_quiesced)
        return AMD_POLARIS_VM_RUNTIME_DEVICE_BUSY;
    if (!amd_polaris_vm_make_ops(&runtime->backend, &ops))
        return AMD_POLARIS_VM_RUNTIME_INVALID_ARGUMENT;

    table_status = gpu_page_table_init_config(table, &ops,
                                               amd_polaris_vm_config());
    if (table_status != GPU_PAGE_TABLE_OK)
        return AMD_POLARIS_VM_RUNTIME_TABLE_INIT_FAILED;
    runtime->root_physical = table->root_physical;

    if (amd_polaris_vm_install_root(runtime->mmio, table->root_physical,
            0U, AMD_POLARIS_VM_DEFAULT_RANGE_SIZE,
            runtime->fault_page_physical, runtime->timeout_iterations, 1) !=
        AMD_POLARIS_VM_HW_OK) {
        /*
         * Hardware setup can fail after some context registers have changed.
         * Disable VMID 1 before releasing any table memory. If disable fails,
         * retain ownership so the caller can retry shutdown.
         */
        if (amd_polaris_vm_disable_context1(runtime->mmio,
                runtime->timeout_iterations, 1) == AMD_POLARIS_VM_HW_OK) {
            if (gpu_page_table_destroy(table, 1) == GPU_PAGE_TABLE_OK)
                runtime->root_physical = 0U;
        }
        return AMD_POLARIS_VM_RUNTIME_HARDWARE_INIT_FAILED;
    }
    return AMD_POLARIS_VM_RUNTIME_OK;
}

amd_polaris_vm_runtime_status_t amd_polaris_vm_runtime_make_vm_ops(
    amd_polaris_vm_runtime_t *runtime, gpu_page_table_t *table,
    gpu_vm_ops_t *out_ops)
{
    if (runtime == 0 || table == 0 || out_ops == 0 ||
        runtime->root_physical == 0U ||
        table->initialized == 0U ||
        table->root_physical != runtime->root_physical ||
        table->ops.context != &runtime->backend)
        return AMD_POLARIS_VM_RUNTIME_INVALID_ARGUMENT;
    if (!gpu_page_table_make_vm_ops(table, out_ops))
        return AMD_POLARIS_VM_RUNTIME_VM_ADAPTER_FAILED;
    return AMD_POLARIS_VM_RUNTIME_OK;
}

amd_polaris_vm_runtime_status_t amd_polaris_vm_runtime_shutdown(
    amd_polaris_vm_runtime_t *runtime, gpu_page_table_t *table,
    int device_quiesced)
{
    if (runtime == 0 || table == 0 || table->initialized == 0U ||
        runtime->root_physical != table->root_physical)
        return AMD_POLARIS_VM_RUNTIME_INVALID_ARGUMENT;
    if (!device_quiesced)
        return AMD_POLARIS_VM_RUNTIME_DEVICE_BUSY;
    if (amd_polaris_vm_disable_context1(runtime->mmio,
            runtime->timeout_iterations, 1) != AMD_POLARIS_VM_HW_OK)
        return AMD_POLARIS_VM_RUNTIME_HARDWARE_SHUTDOWN_FAILED;
    if (gpu_page_table_destroy(table, 1) != GPU_PAGE_TABLE_OK)
        return AMD_POLARIS_VM_RUNTIME_TABLE_DESTROY_FAILED;
    runtime->root_physical = 0U;
    return AMD_POLARIS_VM_RUNTIME_OK;
}
