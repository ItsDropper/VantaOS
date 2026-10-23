/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_page_table.h"

#include <string.h>

static const unsigned index_shifts[GPU_PAGE_TABLE_LEVELS] = {
    39U, 30U, 21U, 12U
};

static int valid_page(const gpu_page_table_page_t *page)
{
    return page != NULL && page->entries != NULL &&
           (page->physical_address & (GPU_PAGE_TABLE_PAGE_SIZE - 1U)) == 0U &&
           (page->physical_address & ~GPU_PAGE_TABLE_ADDRESS_MASK) == 0U;
}

static gpu_page_table_page_t *find_page(gpu_page_table_t *table,
                                        uint64_t physical_address)
{
    for (size_t i = 0U; i < table->page_count; ++i)
        if (table->pages[i].physical_address == physical_address)
            return &table->pages[i];
    return NULL;
}

static gpu_page_table_status_t allocate_table_page(
    gpu_page_table_t *table, gpu_page_table_page_t **out_page)
{
    gpu_page_table_page_t page;
    if (table->page_count >= GPU_PAGE_TABLE_MAX_PAGES)
        return GPU_PAGE_TABLE_TABLE_FULL;
    memset(&page, 0, sizeof(page));
    if (!table->ops.allocate_page(table->ops.context, &page))
        return GPU_PAGE_TABLE_ALLOCATION_FAILED;
    if (!valid_page(&page)) {
        if (page.entries != NULL)
            table->ops.free_page(table->ops.context, &page);
        return GPU_PAGE_TABLE_ADDRESS_INVALID;
    }
    for (size_t i = 0U; i < table->page_count; ++i) {
        if (table->pages[i].physical_address == page.physical_address) {
            table->ops.free_page(table->ops.context, &page);
            return GPU_PAGE_TABLE_CORRUPT;
        }
    }
    memset(page.entries, 0, GPU_PAGE_TABLE_PAGE_SIZE);
    table->pages[table->page_count] = page;
    *out_page = &table->pages[table->page_count++];
    return GPU_PAGE_TABLE_OK;
}

static gpu_page_table_status_t walk_to_leaf(
    gpu_page_table_t *table, uint64_t gpu_address,
    gpu_page_table_page_t **out_leaf, size_t *out_leaf_index)
{
    gpu_page_table_page_t *page = &table->pages[0];
    for (unsigned level = 0U; level < GPU_PAGE_TABLE_LEVELS - 1U; ++level) {
        size_t index = (size_t)((gpu_address >> index_shifts[level]) & 0x1FFU);
        uint64_t entry = page->entries[index];
        gpu_page_table_page_t *child;

        if ((entry & 1U) == 0U) {
            uint64_t encoded;
            gpu_page_table_status_t status;
            if (!table->ops.encode_directory(table->ops.context, 0U, &encoded))
                return GPU_PAGE_TABLE_ENCODE_FAILED;
            /* Allocate first; re-encode with the actual physical address. */
            status = allocate_table_page(table, &child);
            if (status != GPU_PAGE_TABLE_OK)
                return status;
            if (!table->ops.encode_directory(table->ops.context,
                                              child->physical_address,
                                              &encoded)) {
                --table->page_count;
                table->ops.free_page(table->ops.context, child);
                return GPU_PAGE_TABLE_ENCODE_FAILED;
            }
            page->entries[index] = encoded;
        } else {
            uint64_t child_physical = entry & GPU_PAGE_TABLE_ADDRESS_MASK;
            child = find_page(table, child_physical);
            if (child == NULL)
                return GPU_PAGE_TABLE_CORRUPT;
        }
        page = child;
    }
    *out_leaf = page;
    *out_leaf_index = (size_t)((gpu_address >> index_shifts[3]) & 0x1FFU);
    return GPU_PAGE_TABLE_OK;
}

gpu_page_table_status_t gpu_page_table_init(
    gpu_page_table_t *table, const gpu_page_table_ops_t *ops)
{
    gpu_page_table_page_t *root;
    gpu_page_table_status_t status;

    if (table == NULL || ops == NULL || table->initialized != 0U ||
        ops->allocate_page == NULL || ops->free_page == NULL ||
        ops->encode_directory == NULL || ops->encode_leaf == NULL ||
        ops->flush == NULL)
        return GPU_PAGE_TABLE_INVALID_ARGUMENT;
    memset(table, 0, sizeof(*table));
    table->ops = *ops;
    status = allocate_table_page(table, &root);
    if (status != GPU_PAGE_TABLE_OK) {
        memset(table, 0, sizeof(*table));
        return status;
    }
    table->root_physical = root->physical_address;
    table->initialized = 1U;
    return GPU_PAGE_TABLE_OK;
}

gpu_page_table_status_t gpu_page_table_map(
    gpu_page_table_t *table, uint64_t gpu_address, uint64_t physical_address,
    unsigned access)
{
    gpu_page_table_page_t *leaf;
    size_t index;
    uint64_t encoded;
    gpu_page_table_status_t status;

    if (table == NULL || table->initialized == 0U)
        return GPU_PAGE_TABLE_NOT_INITIALIZED;
    if (table->faulted != 0U)
        return GPU_PAGE_TABLE_FAULTED;
    if ((gpu_address & 0xFFFU) != 0U ||
        (physical_address & 0xFFFU) != 0U ||
        gpu_address >= (1ULL << 48U) ||
        (physical_address & ~GPU_PAGE_TABLE_ADDRESS_MASK) != 0U)
        return GPU_PAGE_TABLE_ADDRESS_INVALID;

    status = walk_to_leaf(table, gpu_address, &leaf, &index);
    if (status != GPU_PAGE_TABLE_OK)
        return status;
    if ((leaf->entries[index] & 1U) != 0U)
        return GPU_PAGE_TABLE_ALREADY_MAPPED;
    if (!table->ops.encode_leaf(table->ops.context, physical_address,
                                access, &encoded))
        return GPU_PAGE_TABLE_ENCODE_FAILED;
    leaf->entries[index] = encoded;
    if (!table->ops.flush(table->ops.context, table->root_physical)) {
        /* Translation state is uncertain; prevent further map/unmap operations. */
        table->faulted = 1U;
        return GPU_PAGE_TABLE_FLUSH_FAILED;
    }
    return GPU_PAGE_TABLE_OK;
}

gpu_page_table_status_t gpu_page_table_unmap(
    gpu_page_table_t *table, uint64_t gpu_address)
{
    gpu_page_table_page_t *leaf;
    size_t index;
    gpu_page_table_status_t status;

    if (table == NULL || table->initialized == 0U)
        return GPU_PAGE_TABLE_NOT_INITIALIZED;
    if (table->faulted != 0U)
        return GPU_PAGE_TABLE_FAULTED;
    if ((gpu_address & 0xFFFU) != 0U || gpu_address >= (1ULL << 48U))
        return GPU_PAGE_TABLE_ADDRESS_INVALID;
    status = walk_to_leaf(table, gpu_address, &leaf, &index);
    if (status != GPU_PAGE_TABLE_OK)
        return status;
    if ((leaf->entries[index] & 1U) == 0U)
        return GPU_PAGE_TABLE_NOT_MAPPED;

    leaf->entries[index] = 0U;
    if (!table->ops.flush(table->ops.context, table->root_physical)) {
        table->faulted = 1U;
        return GPU_PAGE_TABLE_FLUSH_FAILED;
    }
    return GPU_PAGE_TABLE_OK;
}

gpu_page_table_status_t gpu_page_table_flush(gpu_page_table_t *table)
{
    if (table == NULL || table->initialized == 0U)
        return GPU_PAGE_TABLE_NOT_INITIALIZED;
    if (table->faulted != 0U)
        return GPU_PAGE_TABLE_FAULTED;
    if (!table->ops.flush(table->ops.context, table->root_physical)) {
        table->faulted = 1U;
        return GPU_PAGE_TABLE_FLUSH_FAILED;
    }
    return GPU_PAGE_TABLE_OK;
}

gpu_page_table_status_t gpu_page_table_destroy(
    gpu_page_table_t *table, int device_quiesced)
{
    if (table == NULL || table->initialized == 0U)
        return GPU_PAGE_TABLE_NOT_INITIALIZED;
    if (!device_quiesced)
        return GPU_PAGE_TABLE_FAULTED;
    for (size_t i = table->page_count; i > 0U; --i)
        table->ops.free_page(table->ops.context, &table->pages[i - 1U]);
    memset(table, 0, sizeof(*table));
    return GPU_PAGE_TABLE_OK;
}
