/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "gpu_page_table.h"

#include <string.h>

static int is_power_of_two(size_t value)
{
    return value != 0U && (value & (value - 1U)) == 0U;
}

static unsigned log2_size(size_t value)
{
    unsigned bits = 0U;
    while (value > 1U) {
        value >>= 1U;
        ++bits;
    }
    return bits;
}

static int valid_config(const gpu_page_table_config_t *config)
{
    unsigned root_bits;
    if (config == NULL || config->level_count < 2U ||
        config->level_count > GPU_PAGE_TABLE_LEVELS ||
        config->virtual_address_bits < 32U ||
        config->virtual_address_bits > 48U ||
        !is_power_of_two(config->root_entry_count))
        return 0;

    root_bits = log2_size(config->root_entry_count);
    if (root_bits == 0U ||
        12U + root_bits + 9U * (config->level_count - 1U) !=
            config->virtual_address_bits)
        return 0;
    if (config->index_shifts[0] !=
        (unsigned)config->virtual_address_bits - root_bits)
        return 0;
    for (unsigned i = 1U; i < config->level_count; ++i) {
        if (config->index_shifts[i] !=
            12U + 9U * (config->level_count - 1U - i))
            return 0;
    }
    return 1;
}

static int valid_page(const gpu_page_table_page_t *page,
                      size_t expected_entries)
{
    size_t required_size;
    if (page == NULL || page->entries == NULL ||
        page->entry_count != expected_entries ||
        expected_entries > SIZE_MAX / sizeof(uint64_t))
        return 0;
    required_size = expected_entries * sizeof(uint64_t);
    return page->allocation_size >= required_size &&
           (page->physical_address & (GPU_PAGE_TABLE_PAGE_SIZE - 1U)) == 0U &&
           (page->physical_address & ~GPU_PAGE_TABLE_ADDRESS_MASK) == 0U &&
           page->allocation_size <= UINT64_MAX - page->physical_address &&
           page->physical_address + page->allocation_size - 1U <=
               GPU_PAGE_TABLE_ADDRESS_MASK + 0xFFFULL;
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
    gpu_page_table_t *table, size_t entry_count,
    gpu_page_table_page_t **out_page)
{
    gpu_page_table_page_t page;
    if (table->page_count >= GPU_PAGE_TABLE_MAX_PAGES)
        return GPU_PAGE_TABLE_TABLE_FULL;
    memset(&page, 0, sizeof(page));
    if (!table->ops.allocate_table(table->ops.context, entry_count, &page))
        return GPU_PAGE_TABLE_ALLOCATION_FAILED;
    if (!valid_page(&page, entry_count)) {
        if (page.entries != NULL)
            table->ops.free_page(table->ops.context, &page);
        return GPU_PAGE_TABLE_ADDRESS_INVALID;
    }
    for (size_t i = 0U; i < table->page_count; ++i) {
        const gpu_page_table_page_t *old = &table->pages[i];
        uint64_t old_end = old->physical_address + old->allocation_size;
        uint64_t new_end = page.physical_address + page.allocation_size;
        if (page.physical_address < old_end &&
            old->physical_address < new_end) {
            table->ops.free_page(table->ops.context, &page);
            return GPU_PAGE_TABLE_CORRUPT;
        }
    }
    memset(page.entries, 0, entry_count * sizeof(uint64_t));
    table->pages[table->page_count] = page;
    *out_page = &table->pages[table->page_count++];
    return GPU_PAGE_TABLE_OK;
}

static gpu_page_table_status_t walk_to_leaf(
    gpu_page_table_t *table, uint64_t gpu_address, int create_missing,
    gpu_page_table_page_t **out_leaf, size_t *out_leaf_index)
{
    gpu_page_table_page_t *page = &table->pages[0];

    for (unsigned level = 0U; level + 1U < table->config.level_count;
         ++level) {
        size_t mask = level == 0U ? table->config.root_entry_count - 1U :
                                   GPU_PAGE_TABLE_ENTRIES - 1U;
        size_t index = (size_t)((gpu_address >>
            table->config.index_shifts[level]) & (uint64_t)mask);
        uint64_t entry = page->entries[index];
        gpu_page_table_page_t *child;

        if ((entry & 1U) == 0U) {
            uint64_t encoded;
            gpu_page_table_status_t status;
            if (!create_missing)
                return GPU_PAGE_TABLE_NOT_MAPPED;
            status = allocate_table_page(table, GPU_PAGE_TABLE_ENTRIES, &child);
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
            if (child == NULL || child->entry_count != GPU_PAGE_TABLE_ENTRIES)
                return GPU_PAGE_TABLE_CORRUPT;
        }
        page = child;
    }

    *out_leaf = page;
    *out_leaf_index = (size_t)((gpu_address >>
        table->config.index_shifts[table->config.level_count - 1U]) & 0x1FFU);
    return GPU_PAGE_TABLE_OK;
}

gpu_page_table_status_t gpu_page_table_init_config(
    gpu_page_table_t *table, const gpu_page_table_ops_t *ops,
    const gpu_page_table_config_t *config)
{
    gpu_page_table_page_t *root;
    gpu_page_table_status_t status;

    if (table == NULL || ops == NULL || table->initialized != 0U ||
        ops->allocate_table == NULL || ops->free_page == NULL ||
        ops->encode_directory == NULL || ops->encode_leaf == NULL ||
        ops->flush == NULL || !valid_config(config))
        return GPU_PAGE_TABLE_INVALID_ARGUMENT;

    memset(table, 0, sizeof(*table));
    table->ops = *ops;
    table->config = *config;
    status = allocate_table_page(table, config->root_entry_count, &root);
    if (status != GPU_PAGE_TABLE_OK) {
        memset(table, 0, sizeof(*table));
        return status;
    }
    table->root_physical = root->physical_address;
    table->initialized = 1U;
    return GPU_PAGE_TABLE_OK;
}

gpu_page_table_status_t gpu_page_table_init(
    gpu_page_table_t *table, const gpu_page_table_ops_t *ops)
{
    const gpu_page_table_config_t config = {
        .level_count = 4U,
        .virtual_address_bits = 48U,
        .root_entry_count = GPU_PAGE_TABLE_ENTRIES,
        .index_shifts = {39U, 30U, 21U, 12U}
    };
    return gpu_page_table_init_config(table, ops, &config);
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
        (gpu_address >> table->config.virtual_address_bits) != 0U ||
        (physical_address & ~GPU_PAGE_TABLE_ADDRESS_MASK) != 0U)
        return GPU_PAGE_TABLE_ADDRESS_INVALID;

    status = walk_to_leaf(table, gpu_address, 1, &leaf, &index);
    if (status != GPU_PAGE_TABLE_OK)
        return status;
    if ((leaf->entries[index] & 1U) != 0U)
        return GPU_PAGE_TABLE_ALREADY_MAPPED;
    if (!table->ops.encode_leaf(table->ops.context, physical_address,
                                access, &encoded))
        return GPU_PAGE_TABLE_ENCODE_FAILED;

    /* Caller batches changes and explicitly flushes before device use. */
    leaf->entries[index] = encoded;
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
    if ((gpu_address & 0xFFFU) != 0U ||
        (gpu_address >> table->config.virtual_address_bits) != 0U)
        return GPU_PAGE_TABLE_ADDRESS_INVALID;
    status = walk_to_leaf(table, gpu_address, 0, &leaf, &index);
    if (status != GPU_PAGE_TABLE_OK)
        return status;
    if ((leaf->entries[index] & 1U) == 0U)
        return GPU_PAGE_TABLE_NOT_MAPPED;
    leaf->entries[index] = 0U;
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
