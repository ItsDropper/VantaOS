/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "amd_polaris_vm.h"
#include "amd_polaris_pte.h"

static int allocate_page(void *context, size_t entry_count,
                         gpu_page_table_page_t *out_page)
{
    amd_polaris_vm_backend_t *backend = context;
    return backend->allocate_table(backend->context, entry_count, out_page);
}

static void free_page(void *context, gpu_page_table_page_t *page)
{
    amd_polaris_vm_backend_t *backend = context;
    backend->free_page(backend->context, page);
}

static int encode_directory(void *context, uint64_t physical, uint64_t *out)
{
    (void)context;
    if (out == 0 || physical == 0U ||
        (physical & 0xFFFU) != 0U ||
        (physical & ~GPU_PAGE_TABLE_ADDRESS_MASK) != 0U)
        return 0;
    *out = physical | AMD_POLARIS_PTE_VALID |
           AMD_POLARIS_PTE_SYSTEM | AMD_POLARIS_PTE_SNOOPED;
    return 1;
}

static int encode_leaf(void *context, uint64_t physical, unsigned access,
                       uint64_t *out)
{
    (void)context;
    return amd_polaris_encode_pte(physical, access, 1, 1, out) ==
           AMD_POLARIS_PTE_OK;
}

static int flush(void *context, uint64_t root_physical)
{
    amd_polaris_vm_backend_t *backend = context;
    return backend->invalidate_tlb(backend->context, root_physical);
}

static const gpu_page_table_config_t amd_config = {
    .level_count = 2U,
    .virtual_address_bits = 36U,
    .root_entry_count = 4U,
    .leaf_entry_count = 1U << 22U,
    .index_shifts = {34U, 12U, 0U, 0U}
};

const gpu_page_table_config_t *amd_polaris_vm_config(void)
{
    return &amd_config;
}

int amd_polaris_vm_make_ops(amd_polaris_vm_backend_t *backend,
                            gpu_page_table_ops_t *out_ops)
{
    if (backend == 0 || out_ops == 0 ||
        backend->allocate_table == 0 || backend->free_page == 0 ||
        backend->invalidate_tlb == 0)
        return 0;
    out_ops->context = backend;
    out_ops->allocate_table = allocate_page;
    out_ops->free_page = free_page;
    out_ops->encode_directory = encode_directory;
    out_ops->encode_leaf = encode_leaf;
    out_ops->flush = flush;
    return 1;
}
