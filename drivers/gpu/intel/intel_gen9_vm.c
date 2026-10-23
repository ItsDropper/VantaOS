/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_gen9_vm.h"
#include "intel_gen9_pte.h"

static int allocate_page(void *context, size_t entry_count,
                         gpu_page_table_page_t *out_page)
{
    intel_gen9_vm_backend_t *backend = context;
    return backend->allocate_table(backend->context, entry_count, out_page);
}

static void free_page(void *context, gpu_page_table_page_t *page)
{
    intel_gen9_vm_backend_t *backend = context;
    backend->free_page(backend->context, page);
}

static int encode_directory(void *context, uint64_t physical, uint64_t *out)
{
    (void)context;
    if (out == 0 || physical == 0U ||
        (physical & 0xFFFU) != 0U ||
        (physical & ~GPU_PAGE_TABLE_ADDRESS_MASK) != 0U)
        return 0;
    *out = physical | INTEL_GEN9_PTE_PRESENT | INTEL_GEN9_PTE_WRITABLE;
    return 1;
}

static int encode_leaf(void *context, uint64_t physical, unsigned access,
                       uint64_t *out)
{
    int writable;
    (void)context;
    if (access != INTEL_GEN9_VM_READ &&
        access != (INTEL_GEN9_VM_READ | INTEL_GEN9_VM_WRITE))
        return 0;
    writable = (access & INTEL_GEN9_VM_WRITE) != 0U;
    return intel_gen9_encode_pte(physical, writable,
        INTEL_GEN9_CACHE_WRITE_BACK, out) == INTEL_GEN9_PTE_OK;
}

static int flush(void *context, uint64_t root_physical)
{
    intel_gen9_vm_backend_t *backend = context;
    return backend->invalidate_tlb(backend->context, root_physical);
}

static const gpu_page_table_config_t intel_config = {
    .level_count = 4U,
    .virtual_address_bits = 48U,
    .root_entry_count = GPU_PAGE_TABLE_ENTRIES,
    .index_shifts = {39U, 30U, 21U, 12U}
};

const gpu_page_table_config_t *intel_gen9_vm_config(void)
{
    return &intel_config;
}

int intel_gen9_vm_make_ops(intel_gen9_vm_backend_t *backend,
                           gpu_page_table_ops_t *out_ops)
{
    if (backend == 0 || out_ops == 0 ||
        backend->allocate_table == 0 || backend->free_page == 0 ||
        backend->invalidate_tlb == 0)
        return 0;
    out_ops->context = backend;
    out_ops->allocate_page = allocate_page;
    out_ops->free_page = free_page;
    out_ops->encode_directory = encode_directory;
    out_ops->encode_leaf = encode_leaf;
    out_ops->flush = flush;
    return 1;
}
