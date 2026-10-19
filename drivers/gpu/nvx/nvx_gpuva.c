/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gpuva.h"

#include <limits.h>

static int nvx_gpuva_add_overflow(uint64_t a, uint64_t b, uint64_t *out)
{
    if (out == 0 || a > UINT64_MAX - b)
        return 1;
    *out = a + b;
    return 0;
}

static int nvx_gpuva_is_power_of_two(uint64_t value)
{
    return value != 0U && (value & (value - 1U)) == 0U;
}

static int nvx_gpuva_align_up(uint64_t value, uint64_t alignment,
                              uint64_t *out)
{
    uint64_t mask;
    if (!nvx_gpuva_is_power_of_two(alignment) || out == 0)
        return 0;
    mask = alignment - 1U;
    if (value > UINT64_MAX - mask)
        return 0;
    *out = (value + mask) & ~mask;
    return 1;
}

static size_t nvx_gpuva_find_free_record(const nvx_gpuva_space_t *space)
{
    for (size_t i = 0; i < NVX_GPUVA_MAX_ALLOCS; ++i)
        if (!space->allocations[i].active)
            return i;
    return NVX_GPUVA_MAX_ALLOCS;
}

static int nvx_gpuva_record_matches(const nvx_gpuva_space_t *space,
                                    const nvx_gpuva_allocation_t *allocation)
{
    if (allocation->token == 0U || allocation->generation != space->generation)
        return 0;
    for (size_t i = 0; i < NVX_GPUVA_MAX_ALLOCS; ++i) {
        const nvx_gpuva_record_t *record = &space->allocations[i];
        if (record->active && record->token == allocation->token)
            return record->generation == allocation->generation &&
                   record->reserved_base == allocation->reserved_base &&
                   record->reserved_size == allocation->reserved_size;
    }
    return 0;
}

/* Insert a free range in base order and coalesce both adjacent neighbours. */
static nvx_gpuva_status_t nvx_gpuva_insert_free(nvx_gpuva_space_t *space,
                                                uint64_t base, uint64_t size)
{
    size_t index = 0;
    uint64_t end;
    if (size == 0U || nvx_gpuva_add_overflow(base, size, &end))
        return NVX_GPUVA_CORRUPT_STATE;

    while (index < space->free_count &&
           space->free_ranges[index].base < base)
        ++index;

    if (index > 0U) {
        uint64_t previous_end;
        if (nvx_gpuva_add_overflow(space->free_ranges[index - 1U].base,
                                   space->free_ranges[index - 1U].size,
                                   &previous_end) ||
            previous_end > base)
            return NVX_GPUVA_CORRUPT_STATE;
    }
    if (index < space->free_count && end > space->free_ranges[index].base)
        return NVX_GPUVA_CORRUPT_STATE;

    if (index > 0U) {
        nvx_gpuva_range_t *previous = &space->free_ranges[index - 1U];
        uint64_t previous_end = previous->base + previous->size;
        if (previous_end == base) {
            previous->size += size;
            if (index < space->free_count &&
                previous->base + previous->size == space->free_ranges[index].base) {
                previous->size += space->free_ranges[index].size;
                for (size_t j = index; j + 1U < space->free_count; ++j)
                    space->free_ranges[j] = space->free_ranges[j + 1U];
                --space->free_count;
            }
            return NVX_GPUVA_OK;
        }
    }

    if (index < space->free_count && end == space->free_ranges[index].base) {
        space->free_ranges[index].base = base;
        space->free_ranges[index].size += size;
        return NVX_GPUVA_OK;
    }

    if (space->free_count >= NVX_GPUVA_MAX_FREE_RANGES)
        return NVX_GPUVA_TABLE_FULL;
    for (size_t j = space->free_count; j > index; --j)
        space->free_ranges[j] = space->free_ranges[j - 1U];
    space->free_ranges[index].base = base;
    space->free_ranges[index].size = size;
    ++space->free_count;
    return NVX_GPUVA_OK;
}

nvx_gpuva_status_t nvx_gpuva_init(nvx_gpuva_space_t *space,
                                  uint64_t start, uint64_t size)
{
    uint64_t end;
    if (space == 0)
        return NVX_GPUVA_INVALID_ARGUMENT;
    if (size == 0U || nvx_gpuva_add_overflow(start, size, &end) ||
        (start & (NVX_GPUVA_PAGE_SIZE - 1U)) != 0U ||
        (size & (NVX_GPUVA_PAGE_SIZE - 1U)) != 0U)
        return NVX_GPUVA_INVALID_RANGE;

    /* Initializing a live space would invalidate all outstanding handles. */
    if (space->initialized)
        return NVX_GPUVA_INVALID_ARGUMENT;

    for (size_t i = 0; i < NVX_GPUVA_MAX_ALLOCS; ++i)
        space->allocations[i].active = 0U;
    spinlock_init(&space->lock);
    space->free_ranges[0].base = start;
    space->free_ranges[0].size = size;
    space->free_count = 1U;
    space->allocation_count = 0U;
    space->start = start;
    space->end = end;
    space->next_token = 1U;
    ++space->generation;
    if (space->generation == 0U)
        space->generation = 1U;
    space->initialized = 1U;
    return NVX_GPUVA_OK;
}

nvx_gpuva_status_t nvx_gpuva_alloc(nvx_gpuva_space_t *space,
                                   uint64_t size, uint64_t alignment,
                                   unsigned guard_pages,
                                   nvx_gpuva_allocation_t *out_allocation)
{
    uint64_t rounded_size;
    uint64_t guard_size;
    uint64_t twice_guard;
    uint64_t needed;
    size_t record_index;
    nvx_gpuva_status_t result = NVX_GPUVA_OUT_OF_SPACE;

    if (out_allocation == 0 || space == 0)
        return NVX_GPUVA_INVALID_ARGUMENT;
    out_allocation->base = 0U;
    out_allocation->size = 0U;
    out_allocation->reserved_base = 0U;
    out_allocation->reserved_size = 0U;
    out_allocation->token = 0U;
    out_allocation->generation = 0U;
    out_allocation->guard_pages = 0U;
    out_allocation->valid = 0U;

    if (!space->initialized)
        return NVX_GPUVA_NOT_INITIALIZED;
    if (size == 0U || !nvx_gpuva_is_power_of_two(alignment) ||
        guard_pages > NVX_GPUVA_GUARD_ONE_PAGE)
        return NVX_GPUVA_INVALID_ARGUMENT;
    if (!nvx_gpuva_align_up(size, NVX_GPUVA_PAGE_SIZE, &rounded_size))
        return NVX_GPUVA_INVALID_RANGE;
    guard_size = guard_pages ? NVX_GPUVA_PAGE_SIZE : 0U;
    if (nvx_gpuva_add_overflow(guard_size, guard_size, &twice_guard) ||
        nvx_gpuva_add_overflow(rounded_size, twice_guard, &needed))
        return NVX_GPUVA_INVALID_RANGE;

    spinlock_lock(&space->lock);
    if (!space->initialized) {
        spinlock_unlock(&space->lock);
        return NVX_GPUVA_NOT_INITIALIZED;
    }
    record_index = nvx_gpuva_find_free_record(space);
    if (record_index == NVX_GPUVA_MAX_ALLOCS) {
        spinlock_unlock(&space->lock);
        return NVX_GPUVA_TABLE_FULL;
    }
    if (space->next_token == 0U || space->next_token == UINT64_MAX) {
        spinlock_unlock(&space->lock);
        return NVX_GPUVA_CORRUPT_STATE;
    }

    for (size_t i = 0; i < space->free_count; ++i) {
        nvx_gpuva_range_t range = space->free_ranges[i];
        uint64_t range_end;
        uint64_t candidate;
        uint64_t user_base;
        uint64_t reserved_end;
        uint64_t prefix;
        uint64_t suffix;

        if (nvx_gpuva_add_overflow(range.base, range.size, &range_end))
            { result = NVX_GPUVA_CORRUPT_STATE; break; }
        if (!nvx_gpuva_add_overflow(range.base, guard_size, &user_base) &&
            nvx_gpuva_align_up(user_base, alignment, &user_base) &&
            user_base >= guard_size) {
            candidate = user_base - guard_size;
        } else {
            continue;
        }
        if (candidate < range.base ||
            nvx_gpuva_add_overflow(candidate, needed, &reserved_end) ||
            reserved_end > range_end)
            continue;
        prefix = candidate - range.base;
        suffix = range_end - reserved_end;

        if (prefix != 0U && suffix != 0U &&
            space->free_count >= NVX_GPUVA_MAX_FREE_RANGES) {
            result = NVX_GPUVA_TABLE_FULL;
            continue;
        }

        if (prefix != 0U && suffix != 0U) {
            for (size_t j = space->free_count; j > i + 1U; --j)
                space->free_ranges[j] = space->free_ranges[j - 1U];
            space->free_ranges[i].size = prefix;
            space->free_ranges[i + 1U].base = reserved_end;
            space->free_ranges[i + 1U].size = suffix;
            ++space->free_count;
        } else if (prefix != 0U) {
            space->free_ranges[i].size = prefix;
        } else if (suffix != 0U) {
            space->free_ranges[i].base = reserved_end;
            space->free_ranges[i].size = suffix;
        } else {
            for (size_t j = i; j + 1U < space->free_count; ++j)
                space->free_ranges[j] = space->free_ranges[j + 1U];
            --space->free_count;
        }

        out_allocation->base = user_base;
        out_allocation->size = rounded_size;
        out_allocation->reserved_base = candidate;
        out_allocation->reserved_size = needed;
        out_allocation->token = space->next_token++;
        out_allocation->generation = space->generation;
        out_allocation->guard_pages = (uint8_t)guard_pages;
        out_allocation->valid = 1U;
        space->allocations[record_index].reserved_base = candidate;
        space->allocations[record_index].reserved_size = needed;
        space->allocations[record_index].token = out_allocation->token;
        space->allocations[record_index].generation = space->generation;
        space->allocations[record_index].active = 1U;
        ++space->allocation_count;
        result = NVX_GPUVA_OK;
        break;
    }
    spinlock_unlock(&space->lock);
    return result;
}

nvx_gpuva_status_t nvx_gpuva_free(nvx_gpuva_space_t *space,
                                  nvx_gpuva_allocation_t *allocation)
{
    nvx_gpuva_status_t result;
    if (space == 0 || allocation == 0)
        return NVX_GPUVA_INVALID_ARGUMENT;
    if (!space->initialized)
        return NVX_GPUVA_NOT_INITIALIZED;
    if (!allocation->valid)
        return NVX_GPUVA_DOUBLE_FREE;

    spinlock_lock(&space->lock);
    if (!nvx_gpuva_record_matches(space, allocation)) {
        spinlock_unlock(&space->lock);
        return NVX_GPUVA_INVALID_HANDLE;
    }

    result = nvx_gpuva_insert_free(space, allocation->reserved_base,
                                   allocation->reserved_size);
    if (result == NVX_GPUVA_OK) {
        for (size_t i = 0; i < NVX_GPUVA_MAX_ALLOCS; ++i) {
            nvx_gpuva_record_t *record = &space->allocations[i];
            if (record->active && record->token == allocation->token) {
                record->active = 0U;
                break;
            }
        }
        --space->allocation_count;
        allocation->valid = 0U;
        allocation->token = 0U;
        allocation->base = 0U;
        allocation->size = 0U;
        allocation->reserved_base = 0U;
        allocation->reserved_size = 0U;
    }
    spinlock_unlock(&space->lock);
    return result;
}

nvx_gpuva_status_t nvx_gpuva_get_stats(nvx_gpuva_space_t *space,
                                       uint64_t *free_bytes,
                                       size_t *free_range_count,
                                       size_t *allocation_count)
{
    uint64_t total = 0U;
    if (space == 0 || free_bytes == 0 || free_range_count == 0 ||
        allocation_count == 0)
        return NVX_GPUVA_INVALID_ARGUMENT;
    if (!space->initialized)
        return NVX_GPUVA_NOT_INITIALIZED;

    spinlock_lock(&space->lock);
    for (size_t i = 0; i < space->free_count; ++i) {
        if (nvx_gpuva_add_overflow(total, space->free_ranges[i].size, &total)) {
            spinlock_unlock(&space->lock);
            return NVX_GPUVA_CORRUPT_STATE;
        }
    }
    *free_bytes = total;
    *free_range_count = space->free_count;
    *allocation_count = space->allocation_count;
    spinlock_unlock(&space->lock);
    return NVX_GPUVA_OK;
}

nvx_gpuva_status_t nvx_gpuva_validate(nvx_gpuva_space_t *space)
{
    uint64_t free_bytes = 0U;
    uint64_t allocated_bytes = 0U;
    size_t active_records = 0U;
    if (space == 0)
        return NVX_GPUVA_INVALID_ARGUMENT;
    if (!space->initialized)
        return NVX_GPUVA_NOT_INITIALIZED;

    spinlock_lock(&space->lock);
    if (space->free_count > NVX_GPUVA_MAX_FREE_RANGES ||
        space->allocation_count > NVX_GPUVA_MAX_ALLOCS ||
        space->start >= space->end) {
        spinlock_unlock(&space->lock);
        return NVX_GPUVA_CORRUPT_STATE;
    }

    for (size_t i = 0; i < space->free_count; ++i) {
        uint64_t end;
        if (space->free_ranges[i].size == 0U ||
            nvx_gpuva_add_overflow(space->free_ranges[i].base,
                                  space->free_ranges[i].size, &end) ||
            space->free_ranges[i].base < space->start ||
            end > space->end ||
            (i > 0U &&
             space->free_ranges[i - 1U].base +
                 space->free_ranges[i - 1U].size >= space->free_ranges[i].base) ||
            nvx_gpuva_add_overflow(free_bytes, space->free_ranges[i].size,
                                   &free_bytes)) {
            spinlock_unlock(&space->lock);
            return NVX_GPUVA_CORRUPT_STATE;
        }
    }

    for (size_t i = 0; i < NVX_GPUVA_MAX_ALLOCS; ++i) {
        const nvx_gpuva_record_t *record = &space->allocations[i];
        uint64_t end;
        if (!record->active)
            continue;
        ++active_records;
        if (record->generation != space->generation ||
            record->reserved_size == 0U ||
            nvx_gpuva_add_overflow(record->reserved_base,
                                   record->reserved_size, &end) ||
            record->reserved_base < space->start || end > space->end ||
            nvx_gpuva_add_overflow(allocated_bytes, record->reserved_size,
                                   &allocated_bytes)) {
            spinlock_unlock(&space->lock);
            return NVX_GPUVA_CORRUPT_STATE;
        }
        for (size_t j = i + 1U; j < NVX_GPUVA_MAX_ALLOCS; ++j) {
            const nvx_gpuva_record_t *other = &space->allocations[j];
            uint64_t other_end;
            if (!other->active)
                continue;
            if (nvx_gpuva_add_overflow(other->reserved_base,
                                       other->reserved_size, &other_end) ||
                (record->reserved_base < other_end &&
                 other->reserved_base < end)) {
                spinlock_unlock(&space->lock);
                return NVX_GPUVA_CORRUPT_STATE;
            }
        }
        for (size_t j = 0; j < space->free_count; ++j) {
            uint64_t free_end = space->free_ranges[j].base +
                                space->free_ranges[j].size;
            if (record->reserved_base < free_end &&
                space->free_ranges[j].base < end) {
                spinlock_unlock(&space->lock);
                return NVX_GPUVA_CORRUPT_STATE;
            }
        }
    }

    if (active_records != space->allocation_count ||
        free_bytes > space->end - space->start ||
        allocated_bytes != (space->end - space->start) - free_bytes) {
        spinlock_unlock(&space->lock);
        return NVX_GPUVA_CORRUPT_STATE;
    }
    spinlock_unlock(&space->lock);
    return NVX_GPUVA_OK;
}
