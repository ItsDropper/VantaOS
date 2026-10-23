/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_page_table_vm.h"

static int map_page(void *context, uint64_t gpu_address,
                    uint64_t physical_address, unsigned access)
{
    return gpu_page_table_map(context, gpu_address, physical_address, access) ==
           GPU_PAGE_TABLE_OK;
}

static int unmap_page(void *context, uint64_t gpu_address)
{
    return gpu_page_table_unmap(context, gpu_address) == GPU_PAGE_TABLE_OK;
}

static int flush(void *context)
{
    return gpu_page_table_flush(context) == GPU_PAGE_TABLE_OK;
}

int gpu_page_table_make_vm_ops(gpu_page_table_t *table, gpu_vm_ops_t *out_ops)
{
    if (table == 0 || out_ops == 0 || table->initialized == 0U ||
        table->faulted != 0U)
        return 0;
    out_ops->context = table;
    out_ops->map_page = map_page;
    out_ops->unmap_page = unmap_page;
    out_ops->flush = flush;
    return 1;
}
