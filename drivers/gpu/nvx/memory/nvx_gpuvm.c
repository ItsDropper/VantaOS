/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gpuvm.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#define NVX_GPUVM_FLUSH_POLL_LIMIT 1000000U
#define NVX_GPUVM_INVALID_TABLE UINT16_MAX
/*
 * GA100/TU102 published dev_mmu.h V2 descriptor fields:
 * PDE address starts at descriptor bit 4 for PA bit 12 (PA >> 8);
 * PDE aperture is bits 32..33 and VOL is bit 34.
 * PTE address uses the same shift; VALID/PRIV/RO are bits 0/1/2,
 * VOL is bit 32, and aperture is bits 33..34.
 */
#define NVX_GPUVM_PDE_SYSTEM_COHERENT (2ULL << 32U)
#define NVX_GPUVM_PDE_VOLATILE (1ULL << 34U)
#define NVX_GPUVM_PTE_VALID 1ULL
#define NVX_GPUVM_PTE_PRIVILEGE (1ULL << 1U)
#define NVX_GPUVM_PTE_READ_ONLY (1ULL << 2U)
#define NVX_GPUVM_PTE_VOLATILE (1ULL << 32U)
#define NVX_GPUVM_PTE_SYSTEM_COHERENT (2ULL << 33U)



static int nvx_gpuvm_add_overflow(uint64_t a, uint64_t b, uint64_t *out)
{
    if (out == 0 || a > UINT64_MAX - b)
        return 1;
    *out = a + b;
    return 0;
}

static int nvx_gpuvm_page_address_valid(uint64_t address)
{
    return address != 0U &&
           (address & (NVX_GPUVM_PAGE_SIZE - 1U)) == 0U &&
           address < (1ULL << NVX_GPUVM_PA_BITS);
}

static void nvx_gpuvm_clear_mapping(nvx_gpuvm_mapping_t *mapping)
{
    mapping->token = 0U;
    mapping->base = 0U;
    mapping->size = 0U;
    mapping->generation = 0U;
    mapping->valid = 0U;
    mapping->mapped = 0U;
}

static size_t nvx_gpuvm_find_record(const nvx_gpuvm_t *vm, uint64_t token)
{
    for (size_t i = 0; i < NVX_GPUVM_MAX_MAPPINGS; ++i)
        if (vm->mappings[i].active && vm->mappings[i].token == token)
            return i;
    return NVX_GPUVM_MAX_MAPPINGS;
}

static size_t nvx_gpuvm_find_free_record(const nvx_gpuvm_t *vm)
{
    for (size_t i = 0; i < NVX_GPUVM_MAX_MAPPINGS; ++i)
        if (!vm->mappings[i].active)
            return i;
    return NVX_GPUVM_MAX_MAPPINGS;
}

static size_t nvx_gpuvm_find_free_table(const nvx_gpuvm_t *vm)
{
    for (size_t i = 0; i < NVX_GPUVM_MAX_TABLES; ++i)
        if (!vm->tables[i].active)
            return i;
    return NVX_GPUVM_MAX_TABLES;
}

static uint16_t nvx_gpuvm_index_for_level(uint64_t va, uint8_t level)
{
    switch (level) {
        case 4: return (uint16_t)((va >> 47U) & 0x3U);
        case 3: return (uint16_t)((va >> 38U) & 0x1FFU);
        case 2: return (uint16_t)((va >> 29U) & 0x1FFU);
        case 1: return (uint16_t)((va >> 21U) & 0xFFU);
        case 0: return (uint16_t)((va >> 12U) & 0x1FFU);
        default: return UINT16_MAX;
    }
}

static uint64_t nvx_gpuvm_pde_value(uint64_t physical_address)
{
    /* V2 PDE address field begins at descriptor bit 4: PA is shifted by 8. */
    return (physical_address >> 8U) |
           NVX_GPUVM_PDE_SYSTEM_COHERENT |
           NVX_GPUVM_PDE_VOLATILE;
}

static uint64_t nvx_gpuvm_pte_value(uint64_t physical_address, unsigned access)
{
    uint64_t value = (physical_address >> 8U) | NVX_GPUVM_PTE_VALID |
                     NVX_GPUVM_PTE_PRIVILEGE | NVX_GPUVM_PTE_VOLATILE |
                     NVX_GPUVM_PTE_SYSTEM_COHERENT;
    if ((access & NVX_GPUVM_ACCESS_WRITE) == 0U)
        value |= NVX_GPUVM_PTE_READ_ONLY;
    return value;
}

static nvx_gpuvm_status_t nvx_gpuvm_alloc_table(nvx_gpuvm_t *vm,
                                                 uint8_t level,
                                                 uint16_t parent_index,
                                                 uint16_t parent_slot,
                                                 uint16_t *out_index)
{
    size_t index;
    dma_page_t page = {0};
    if (out_index == 0 || level > 4U)
        return NVX_GPUVM_INVALID_ARGUMENT;
    index = nvx_gpuvm_find_free_table(vm);
    if (index == NVX_GPUVM_MAX_TABLES)
        return NVX_GPUVM_TABLES_EXHAUSTED;
    if (!dma_alloc_page(&page))
        return NVX_GPUVM_DMA_FAILED;
    if (!dma_page_is_valid(&page) ||
        !nvx_gpuvm_page_address_valid(page.physical_address) ||
        page.virtual_address == 0) {
        dma_free_page(&page);
        return NVX_GPUVM_PAGE_INVALID;
    }

    volatile uint64_t *words = (volatile uint64_t *)page.virtual_address;
    for (size_t i = 0; i < NVX_GPUVM_PAGE_SIZE / sizeof(uint64_t); ++i)
        words[i] = 0U;
    vm->tables[index].backing = page;
    vm->tables[index].parent_index = parent_index;
    vm->tables[index].parent_slot = parent_slot;
    vm->tables[index].entry_count = 0U;
    vm->tables[index].level = level;
    vm->tables[index].active = 1U;
    ++vm->table_count;
    *out_index = (uint16_t)index;
    return NVX_GPUVM_OK;
}

static uint64_t *nvx_gpuvm_parent_entry(nvx_gpuvm_t *vm,
                                        uint16_t parent_index,
                                        uint16_t slot)
{
    nvx_gpuvm_table_t *parent;
    if (parent_index >= NVX_GPUVM_MAX_TABLES)
        return 0;
    parent = &vm->tables[parent_index];
    if (!parent->active || parent->level == 0U)
        return 0;
    /* Published GA100/TU102 V2 PDE descriptors are 64 bits at every level. */
    return (uint64_t *)((uintptr_t)parent->backing.virtual_address +
                        (uintptr_t)slot * sizeof(uint64_t));
}

static size_t nvx_gpuvm_find_child(const nvx_gpuvm_t *vm,
                                   uint16_t parent, uint16_t slot)
{
    for (size_t i = 0; i < NVX_GPUVM_MAX_TABLES; ++i) {
        const nvx_gpuvm_table_t *table = &vm->tables[i];
        if (table->active && table->parent_index == parent &&
            table->parent_slot == slot)
            return i;
    }
    return NVX_GPUVM_MAX_TABLES;
}

static nvx_gpuvm_status_t nvx_gpuvm_get_child(nvx_gpuvm_t *vm,
                                               uint16_t parent_index,
                                               uint16_t slot,
                                               uint16_t *out_child)
{
    nvx_gpuvm_table_t *parent;
    uint64_t *entry;
    size_t child_index;
    uint16_t created;
    nvx_gpuvm_status_t status;
    if (out_child == 0 || parent_index >= NVX_GPUVM_MAX_TABLES)
        return NVX_GPUVM_INVALID_ARGUMENT;
    parent = &vm->tables[parent_index];
    if (!parent->active || parent->level == 0U)
        return NVX_GPUVM_CORRUPT_STATE;
    entry = nvx_gpuvm_parent_entry(vm, parent_index, slot);
    if (entry == 0)
        return NVX_GPUVM_CORRUPT_STATE;

    child_index = nvx_gpuvm_find_child(vm, parent_index, slot);
    if (child_index != NVX_GPUVM_MAX_TABLES) {
        if (*entry != nvx_gpuvm_pde_value(
                vm->tables[child_index].backing.physical_address))
            return NVX_GPUVM_CORRUPT_STATE;
        *out_child = (uint16_t)child_index;
        return NVX_GPUVM_OK;
    }
    if (*entry != 0U)
        return NVX_GPUVM_CORRUPT_STATE;

    status = nvx_gpuvm_alloc_table(vm, (uint8_t)(parent->level - 1U),
                                   parent_index, slot, &created);
    if (status != NVX_GPUVM_OK)
        return status;

    *entry = nvx_gpuvm_pde_value(vm->tables[created].backing.physical_address);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    ++parent->entry_count;
    *out_child = created;
    return NVX_GPUVM_OK;
}

static nvx_gpuvm_status_t nvx_gpuvm_leaf_for_va(nvx_gpuvm_t *vm,
                                                 uint64_t va,
                                                 int create,
                                                 uint16_t *out_leaf)
{
    uint16_t current = vm->root_index;
    for (uint8_t level = 4U; level > 0U; --level) {
        uint16_t child;
        uint16_t slot = nvx_gpuvm_index_for_level(va, level);
        nvx_gpuvm_status_t status;
        if (slot == UINT16_MAX)
            return NVX_GPUVM_INVALID_RANGE;
        if (!create) {
            size_t index = nvx_gpuvm_find_child(vm, current, slot);
            if (index == NVX_GPUVM_MAX_TABLES)
                return NVX_GPUVM_NOT_MAPPED;
            current = (uint16_t)index;
            continue;
        }
        status = nvx_gpuvm_get_child(vm, current, slot, &child);
        if (status != NVX_GPUVM_OK)
            return status;
        current = child;
    }
    if (vm->tables[current].level != 0U)
        return NVX_GPUVM_CORRUPT_STATE;
    *out_leaf = current;
    return NVX_GPUVM_OK;
}

static nvx_gpuvm_status_t nvx_gpuvm_prune_empty_path(nvx_gpuvm_t *vm,
                                                        uint64_t va)
{
    uint16_t path[5];
    size_t depth=1U;
    uint16_t current=vm->root_index;
    path[0]=current;
    for(uint8_t level=4U;level>0U;--level) {
        uint16_t slot=nvx_gpuvm_index_for_level(va,level);
        size_t child;
        if(slot==UINT16_MAX) return NVX_GPUVM_INVALID_RANGE;
        child=nvx_gpuvm_find_child(vm,current,slot);
        if(child==NVX_GPUVM_MAX_TABLES) break;
        if(depth>=5U||!vm->tables[child].active)
            return NVX_GPUVM_CORRUPT_STATE;
        path[depth++]=(uint16_t)child;
        current=(uint16_t)child;
    }
    while(depth>1U) {
        uint16_t child_index=path[depth-1U];
        uint16_t parent_index=path[depth-2U];
        nvx_gpuvm_table_t *table=&vm->tables[child_index];
        nvx_gpuvm_table_t *parent=&vm->tables[parent_index];
        uint16_t limit;
        uint64_t *entry;
        if(table->entry_count!=0U) break;
        for(size_t i=0;i<NVX_GPUVM_MAX_TABLES;i++)
            if(vm->tables[i].active&&vm->tables[i].parent_index==child_index)
                return NVX_GPUVM_CORRUPT_STATE;
        if(!table->active||table->parent_index!=parent_index||
           !parent->active||parent->level!=table->level+1U)
            return NVX_GPUVM_CORRUPT_STATE;
        limit=parent->level==4U?4U:(parent->level==1U?256U:512U);
        if(table->parent_slot>=limit||parent->entry_count==0U)
            return NVX_GPUVM_CORRUPT_STATE;
        entry=nvx_gpuvm_parent_entry(vm,parent_index,table->parent_slot);
        if(!entry||entry[0]!=nvx_gpuvm_pde_value(table->backing.physical_address))
            return NVX_GPUVM_CORRUPT_STATE;
        entry[0]=0U;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        --parent->entry_count;
        dma_free_page(&table->backing);
        table->active=0U;
        table->entry_count=0U;
        if(vm->table_count==0U) return NVX_GPUVM_CORRUPT_STATE;
        --vm->table_count;
        --depth;
    }
    return NVX_GPUVM_OK;
}

static uint64_t *nvx_gpuvm_leaf_entry(nvx_gpuvm_t *vm, uint16_t leaf,
                                      uint64_t va)
{
    uint16_t slot = nvx_gpuvm_index_for_level(va, 0U);
    if (leaf >= NVX_GPUVM_MAX_TABLES || !vm->tables[leaf].active ||
        vm->tables[leaf].level != 0U || slot == UINT16_MAX)
        return 0;
    return (uint64_t *)((uintptr_t)vm->tables[leaf].backing.virtual_address +
                        (uintptr_t)slot * sizeof(uint64_t));
}

static nvx_gpuvm_status_t nvx_gpuvm_flush_root_locked(
    nvx_gpuvm_t *vm, uint64_t root_address)
{
    nvx_gpuvm_status_t status;
    if (vm->hw.publish_root == 0 || vm->hw.invalidate == 0)
        return NVX_GPUVM_MMIO_ERROR;
    if (root_address != 0U &&
        !nvx_gpuvm_page_address_valid(root_address))
        return NVX_GPUVM_ADDRESS_WIDTH;

    status = vm->hw.publish_root(vm->hw.context, root_address);
    if (status != NVX_GPUVM_OK)
        return status;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    return vm->hw.invalidate(vm->hw.context);
}

static nvx_gpuvm_status_t nvx_gpuvm_flush_locked(nvx_gpuvm_t *vm,
                                                  uint64_t root_address)
{
    nvx_gpuvm_status_t status = nvx_gpuvm_flush_root_locked(vm, root_address);
    if (status != NVX_GPUVM_OK) {
        vm->faulted = 1U;
        return status;
    }
    vm->root_published = root_address != 0U;
    return NVX_GPUVM_OK;
}

static nvx_gpuvm_record_t *nvx_gpuvm_validate_handle(
    nvx_gpuvm_t *vm, const nvx_gpuvm_mapping_t *mapping)
{
    size_t index;
    if (mapping == 0 || !mapping->valid || mapping->token == 0U ||
        mapping->generation != vm->generation)
        return 0;
    index = nvx_gpuvm_find_record(vm, mapping->token);
    if (index == NVX_GPUVM_MAX_MAPPINGS)
        return 0;
    nvx_gpuvm_record_t *record = &vm->mappings[index];
    if (record->generation != mapping->generation ||
        record->va.base != mapping->base || record->va.size != mapping->size ||
        record->mapped != mapping->mapped)
        return 0;
    return record;
}

nvx_gpuvm_status_t nvx_gpuvm_init(nvx_gpuvm_t *vm, uint64_t va_start,
                                  uint64_t va_size,
                                  const nvx_gpuvm_hw_ops_t *hw)
{
    uint64_t va_end;
    uint16_t root;
    nvx_gpuvm_status_t status;
    nvx_gpuva_status_t va_status;
    if (vm == 0 || hw == 0 || hw->publish_root == 0 || hw->invalidate == 0)
        return NVX_GPUVM_INVALID_ARGUMENT;
    if (vm->initialized)
        return NVX_GPUVM_INVALID_ARGUMENT;
    if (va_size == 0U || nvx_gpuvm_add_overflow(va_start, va_size, &va_end) ||
        va_end > (1ULL << NVX_GPUVM_VA_BITS) ||
        (va_start & (NVX_GPUVM_PAGE_SIZE - 1U)) != 0U ||
        (va_size & (NVX_GPUVM_PAGE_SIZE - 1U)) != 0U)
        return NVX_GPUVM_INVALID_RANGE;

    spinlock_init(&vm->lock);
    va_status = nvx_gpuva_init(&vm->va_space, va_start, va_size);
    if (va_status != NVX_GPUVA_OK)
        return NVX_GPUVM_INVALID_RANGE;
    vm->hw = *hw;
    vm->next_token = 1U;
    ++vm->generation;
    if (vm->generation == 0U)
        vm->generation = 1U;
    vm->table_count = 0U;
    vm->mapping_count = 0U;
    vm->root_published = 0U;
    vm->quiesced = 1U;
    vm->faulted = 0U;
    for (size_t i = 0; i < NVX_GPUVM_MAX_TABLES; ++i)
        vm->tables[i].active = 0U;
    for (size_t i = 0; i < NVX_GPUVM_MAX_MAPPINGS; ++i)
        vm->mappings[i].active = 0U;

    status = nvx_gpuvm_alloc_table(vm, 4U, NVX_GPUVM_INVALID_TABLE, 0U, &root);
    if (status != NVX_GPUVM_OK) {
        vm->va_space.initialized = 0U;
        return status;
    }
    vm->root_index = root;
    vm->initialized = 1U;
    return NVX_GPUVM_OK;
}

nvx_gpuvm_status_t nvx_gpuvm_reserve(nvx_gpuvm_t *vm, uint64_t size,
                                     uint64_t alignment, unsigned guard_pages,
                                     nvx_gpuvm_mapping_t *out_mapping)
{
    nvx_gpuva_allocation_t va = {0};
    nvx_gpuva_status_t va_status;
    size_t record_index;
    if (out_mapping != 0)
        nvx_gpuvm_clear_mapping(out_mapping);
    if (vm == 0 || out_mapping == 0)
        return NVX_GPUVM_INVALID_ARGUMENT;
    if (!vm->initialized)
        return NVX_GPUVM_NOT_INITIALIZED;
    if (vm->faulted)
        return NVX_GPUVM_DEVICE_FAULTED;

    spinlock_lock(&vm->lock);
    record_index = nvx_gpuvm_find_free_record(vm);
    if (record_index == NVX_GPUVM_MAX_MAPPINGS ||
        vm->next_token == 0U || vm->next_token == UINT64_MAX) {
        spinlock_unlock(&vm->lock);
        return NVX_GPUVM_OUT_OF_VA;
    }
    va_status = nvx_gpuva_alloc(&vm->va_space, size, alignment,
                                guard_pages, &va);
    if (va_status != NVX_GPUVA_OK) {
        spinlock_unlock(&vm->lock);
        return NVX_GPUVM_OUT_OF_VA;
    }

    nvx_gpuvm_record_t *record = &vm->mappings[record_index];
    record->va = va;
    record->token = vm->next_token++;
    record->page_count = va.size / NVX_GPUVM_PAGE_SIZE;
    record->generation = vm->generation;
    record->mapped = 0U;
    record->active = 1U;
    ++vm->mapping_count;

    out_mapping->token = record->token;
    out_mapping->base = va.base;
    out_mapping->size = va.size;
    out_mapping->generation = vm->generation;
    out_mapping->valid = 1U;
    out_mapping->mapped = 0U;
    spinlock_unlock(&vm->lock);
    return NVX_GPUVM_OK;
}

nvx_gpuvm_status_t nvx_gpuvm_map_pages(nvx_gpuvm_t *vm,
                                       nvx_gpuvm_mapping_t *mapping,
                                       const uint64_t *physical_pages,
                                       size_t page_count, unsigned access)
{
    nvx_gpuvm_record_t *record;
    size_t mapped = 0U;
    nvx_gpuvm_status_t status = NVX_GPUVM_OK;
    if (vm == 0 || mapping == 0 || physical_pages == 0)
        return NVX_GPUVM_INVALID_ARGUMENT;
    if (!vm->initialized)
        return NVX_GPUVM_NOT_INITIALIZED;
    if (vm->faulted)
        return NVX_GPUVM_DEVICE_FAULTED;
    if ((access & NVX_GPUVM_ACCESS_READ) == 0U ||
        (access & ~(NVX_GPUVM_ACCESS_READ | NVX_GPUVM_ACCESS_WRITE)) != 0U)
        return NVX_GPUVM_INVALID_ARGUMENT;

    spinlock_lock(&vm->lock);
    if (vm->root_published && !vm->quiesced) {
        spinlock_unlock(&vm->lock);
        return NVX_GPUVM_BUSY;
    }
    record = nvx_gpuvm_validate_handle(vm, mapping);
    if (record == 0) {
        spinlock_unlock(&vm->lock);
        return NVX_GPUVM_STALE_HANDLE;
    }
    if (record->mapped) {
        spinlock_unlock(&vm->lock);
        return NVX_GPUVM_ALREADY_MAPPED;
    }
    if (page_count != record->page_count) {
        spinlock_unlock(&vm->lock);
        return NVX_GPUVM_INVALID_RANGE;
    }
    for (size_t i = 0; i < page_count; ++i) {
        if (physical_pages[i] == 0U ||
            (physical_pages[i] & (NVX_GPUVM_PAGE_SIZE - 1U)) != 0U) {
            spinlock_unlock(&vm->lock);
            return NVX_GPUVM_PAGE_INVALID;
        }
        if (physical_pages[i] >= (1ULL << NVX_GPUVM_PA_BITS)) {
            spinlock_unlock(&vm->lock);
            return NVX_GPUVM_ADDRESS_WIDTH;
        }
    }

    for (; mapped < page_count; ++mapped) {
        uint64_t va = record->va.base + mapped * NVX_GPUVM_PAGE_SIZE;
        uint16_t leaf;
        uint64_t *entry;
        status = nvx_gpuvm_leaf_for_va(vm, va, 1, &leaf);
        if (status != NVX_GPUVM_OK)
            break;
        entry = nvx_gpuvm_leaf_entry(vm, leaf, va);
        if (entry == 0 || *entry != 0U) {
            status = entry == 0 ? NVX_GPUVM_CORRUPT_STATE
                                : NVX_GPUVM_ALREADY_MAPPED;
            break;
        }
        __atomic_store_n(entry, nvx_gpuvm_pte_value(physical_pages[mapped], access),
                         __ATOMIC_RELEASE);
        ++vm->tables[leaf].entry_count;
    }

    if (status != NVX_GPUVM_OK) {
        while (mapped > 0U) {
            uint64_t va;
            uint16_t leaf;
            uint64_t *entry;
            --mapped;
            va = record->va.base + mapped * NVX_GPUVM_PAGE_SIZE;
            if (nvx_gpuvm_leaf_for_va(vm, va, 0, &leaf) == NVX_GPUVM_OK) {
                entry = nvx_gpuvm_leaf_entry(vm, leaf, va);
                if (entry != 0 && *entry != 0U) {
                    __atomic_store_n(entry, 0U, __ATOMIC_RELEASE);
                    --vm->tables[leaf].entry_count;
                }
            }
        }
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (vm->root_published &&
            (status = nvx_gpuvm_flush_root_locked(
                vm, vm->tables[vm->root_index].backing.physical_address)) !=
                NVX_GPUVM_OK) {
            vm->faulted = 1U;
        } else {
            for (size_t i=0;i<record->page_count;i++) {
                uint64_t va=record->va.base+i*NVX_GPUVM_PAGE_SIZE;
                nvx_gpuvm_status_t prune_status=nvx_gpuvm_prune_empty_path(vm,va);
                if(prune_status!=NVX_GPUVM_OK) {
                    vm->faulted=1U;
                    status=NVX_GPUVM_CORRUPT_STATE;
                    break;
                }
            }
        }
        spinlock_unlock(&vm->lock);
        return status;
    }

    record->mapped = 1U;
    mapping->mapped = 1U;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (vm->root_published) {
        status = nvx_gpuvm_flush_locked(
            vm, vm->tables[vm->root_index].backing.physical_address);
        if (status != NVX_GPUVM_OK) {
            spinlock_unlock(&vm->lock);
            return status;
        }
    }
    spinlock_unlock(&vm->lock);
    return NVX_GPUVM_OK;
}

nvx_gpuvm_status_t nvx_gpuvm_release(nvx_gpuvm_t *vm,
                                     nvx_gpuvm_mapping_t *mapping)
{
    nvx_gpuvm_record_t *record;
    nvx_gpuvm_status_t status = NVX_GPUVM_OK;
    if (vm == 0 || mapping == 0)
        return NVX_GPUVM_INVALID_ARGUMENT;
    if (!vm->initialized)
        return NVX_GPUVM_NOT_INITIALIZED;
    if (vm->faulted)
        return NVX_GPUVM_DEVICE_FAULTED;

    spinlock_lock(&vm->lock);
    record = nvx_gpuvm_validate_handle(vm, mapping);
    if (record == 0) {
        spinlock_unlock(&vm->lock);
        return NVX_GPUVM_STALE_HANDLE;
    }
    if (record->mapped && vm->root_published && !vm->quiesced) {
        spinlock_unlock(&vm->lock);
        return NVX_GPUVM_BUSY;
    }

    if (record->mapped) {
        for (uint64_t page = 0; page < record->page_count; ++page) {
            uint64_t va = record->va.base + page * NVX_GPUVM_PAGE_SIZE;
            uint16_t leaf;
            uint64_t *entry;
            status = nvx_gpuvm_leaf_for_va(vm, va, 0, &leaf);
            if (status != NVX_GPUVM_OK) {
                vm->faulted = 1U;
                spinlock_unlock(&vm->lock);
                return NVX_GPUVM_CORRUPT_STATE;
            }
            entry = nvx_gpuvm_leaf_entry(vm, leaf, va);
            if (entry == 0 || *entry == 0U ||
                vm->tables[leaf].entry_count == 0U) {
                vm->faulted = 1U;
                spinlock_unlock(&vm->lock);
                return NVX_GPUVM_CORRUPT_STATE;
            }
            __atomic_store_n(entry, 0U, __ATOMIC_RELEASE);
            --vm->tables[leaf].entry_count;
        }
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (vm->root_published) {
            status = nvx_gpuvm_flush_locked(
                vm, vm->tables[vm->root_index].backing.physical_address);
            if (status != NVX_GPUVM_OK) {
                spinlock_unlock(&vm->lock);
                return status;
            }
        }
        for(uint64_t page=0;page<record->page_count;page++) {
            uint64_t va=record->va.base+page*NVX_GPUVM_PAGE_SIZE;
            status=nvx_gpuvm_prune_empty_path(vm,va);
            if(status!=NVX_GPUVM_OK) {
                vm->faulted=1U;
                spinlock_unlock(&vm->lock);
                return NVX_GPUVM_CORRUPT_STATE;
            }
        }
    }

    status = nvx_gpuva_free(&vm->va_space, &record->va) == NVX_GPUVA_OK
        ? NVX_GPUVM_OK : NVX_GPUVM_CORRUPT_STATE;
    if (status == NVX_GPUVM_OK) {
        record->active = 0U;
        record->mapped = 0U;
        record->token = 0U;
        record->page_count = 0U;
        --vm->mapping_count;
        nvx_gpuvm_clear_mapping(mapping);
    }
    spinlock_unlock(&vm->lock);
    return status;
}

nvx_gpuvm_status_t nvx_gpuvm_activate(nvx_gpuvm_t *vm)
{
    nvx_gpuvm_status_t status;
    if (vm == 0)
        return NVX_GPUVM_INVALID_ARGUMENT;
    if (!vm->initialized)
        return NVX_GPUVM_NOT_INITIALIZED;
    if (vm->faulted)
        return NVX_GPUVM_DEVICE_FAULTED;
    spinlock_lock(&vm->lock);
    if (!vm->quiesced) {
        spinlock_unlock(&vm->lock);
        return NVX_GPUVM_BUSY;
    }
    status = nvx_gpuvm_flush_locked(
        vm, vm->tables[vm->root_index].backing.physical_address);
    spinlock_unlock(&vm->lock);
    return status;
}

nvx_gpuvm_status_t nvx_gpuvm_flush(nvx_gpuvm_t *vm)
{
    return nvx_gpuvm_activate(vm);
}

nvx_gpuvm_status_t nvx_gpuvm_set_quiesced(nvx_gpuvm_t *vm, int quiesced)
{
    if (vm == 0)
        return NVX_GPUVM_INVALID_ARGUMENT;
    if (!vm->initialized)
        return NVX_GPUVM_NOT_INITIALIZED;

    spinlock_lock(&vm->lock);
    if (vm->faulted) {
        spinlock_unlock(&vm->lock);
        return NVX_GPUVM_DEVICE_FAULTED;
    }
    if (!quiesced && !vm->root_published) {
        spinlock_unlock(&vm->lock);
        return NVX_GPUVM_BUSY;
    }
    __atomic_store_n(&vm->quiesced, quiesced ? 1U : 0U, __ATOMIC_RELEASE);
    spinlock_unlock(&vm->lock);
    return NVX_GPUVM_OK;
}


static int nvx_gpuvm_table_entries_consistent(const nvx_gpuvm_t *vm,
                                               size_t table_index)
{
    const nvx_gpuvm_table_t *table = &vm->tables[table_index];
    const volatile uint64_t *words =
        (const volatile uint64_t *)table->backing.virtual_address;
    size_t nonzero_entries = 0U;
    size_t child_tables = 0U;
    size_t limit;

    for (size_t i = 0; i < NVX_GPUVM_MAX_TABLES; ++i)
        if (vm->tables[i].active &&
            vm->tables[i].parent_index == table_index)
            ++child_tables;

    if (table->level == 0U) {
        limit = 512U;
        for (size_t i = 0; i < limit; ++i) {
            uint64_t entry = words[i];
            if (entry != 0U) {
                if ((entry & NVX_GPUVM_PTE_VALID) == 0U)
                    return 0;
                ++nonzero_entries;
            }
        }
        return child_tables == 0U &&
               nonzero_entries == table->entry_count;
    }

    limit = table->level == 4U ? 512U :
            table->level == 1U ? 256U : 512U;
    for (size_t i = 0; i < limit; ++i) {
        const uint64_t *entry = (const uint64_t *)(
            (uintptr_t)table->backing.virtual_address +
            (uintptr_t)i * (table->level == 1U ? 16U : 8U));
        if (table->level == 4U && i >= 4U) {
            if (entry[0] != 0U)
                return 0;
            continue;
        }
        if (table->level == 1U && entry[1] != 0U)
            return 0;
        if (entry[0] != 0U)
            ++nonzero_entries;
    }

    return nonzero_entries == table->entry_count &&
           nonzero_entries == child_tables;
}

nvx_gpuvm_status_t nvx_gpuvm_validate(nvx_gpuvm_t *vm)
{
    size_t active_tables = 0U;
    size_t active_mappings = 0U;
    if (vm == 0)
        return NVX_GPUVM_INVALID_ARGUMENT;
    if (!vm->initialized)
        return NVX_GPUVM_NOT_INITIALIZED;
    spinlock_lock(&vm->lock);
    if (vm->root_index >= NVX_GPUVM_MAX_TABLES ||
        !vm->tables[vm->root_index].active) {
        spinlock_unlock(&vm->lock);
        return NVX_GPUVM_CORRUPT_STATE;
    }
    for (size_t i = 0; i < NVX_GPUVM_MAX_TABLES; ++i) {
        nvx_gpuvm_table_t *table = &vm->tables[i];
        if (!table->active)
            continue;
        ++active_tables;
        if (!dma_page_is_valid(&table->backing) ||
            !nvx_gpuvm_page_address_valid(table->backing.physical_address) ||
            table->backing.physical_address > UINT32_MAX ||
            table->level > 4U ||
            (i == vm->root_index && table->level != 4U) ||
            (i != vm->root_index &&
             (table->parent_index >= NVX_GPUVM_MAX_TABLES ||
              !vm->tables[table->parent_index].active ||
              vm->tables[table->parent_index].level != table->level + 1U))) {
            spinlock_unlock(&vm->lock);
            return NVX_GPUVM_CORRUPT_STATE;
        }
        if (!nvx_gpuvm_table_entries_consistent(vm, i)) {
            spinlock_unlock(&vm->lock);
            return NVX_GPUVM_CORRUPT_STATE;
        }
        if (i != vm->root_index) {
            uint64_t *entry = nvx_gpuvm_parent_entry(
                vm, table->parent_index, table->parent_slot);
            if (entry == 0 || *entry !=
                nvx_gpuvm_pde_value(table->backing.physical_address)) {
                spinlock_unlock(&vm->lock);
                return NVX_GPUVM_CORRUPT_STATE;
            }
        }
    }
    for (size_t i = 0; i < NVX_GPUVM_MAX_MAPPINGS; ++i) {
        nvx_gpuvm_record_t *record = &vm->mappings[i];
        if (!record->active)
            continue;
        ++active_mappings;
        if (record->token == 0U || record->generation != vm->generation ||
            record->page_count != record->va.size / NVX_GPUVM_PAGE_SIZE ||
            record->va.valid == 0U) {
            spinlock_unlock(&vm->lock);
            return NVX_GPUVM_CORRUPT_STATE;
        }
    }
    if (active_tables != vm->table_count ||
        active_mappings != vm->mapping_count ||
        active_tables == 0U || !vm->tables[vm->root_index].active) {
        spinlock_unlock(&vm->lock);
        return NVX_GPUVM_CORRUPT_STATE;
    }
    spinlock_unlock(&vm->lock);
    return nvx_gpuva_validate(&vm->va_space) == NVX_GPUVA_OK
        ? NVX_GPUVM_OK : NVX_GPUVM_CORRUPT_STATE;
}

nvx_gpuvm_status_t nvx_gpuvm_destroy(nvx_gpuvm_t *vm)
{
    nvx_gpuvm_status_t status;
    if (vm == 0)
        return NVX_GPUVM_INVALID_ARGUMENT;
    if (!vm->initialized)
        return NVX_GPUVM_NOT_INITIALIZED;
    if (vm->faulted)
        return NVX_GPUVM_DEVICE_FAULTED;

    spinlock_lock(&vm->lock);
    if (!vm->quiesced || vm->mapping_count != 0U) {
        spinlock_unlock(&vm->lock);
        return NVX_GPUVM_BUSY;
    }
    if (vm->root_published) {
        status = nvx_gpuvm_flush_locked(vm, 0U);
        if (status != NVX_GPUVM_OK) {
            spinlock_unlock(&vm->lock);
            return status;
        }
    }
    for (size_t i = 0; i < NVX_GPUVM_MAX_TABLES; ++i) {
        if (vm->tables[i].active) {
            dma_free_page(&vm->tables[i].backing);
            vm->tables[i].active = 0U;
        }
    }
    vm->table_count = 0U;
    vm->va_space.initialized = 0U;
    vm->initialized = 0U;
    vm->root_published = 0U;
    spinlock_unlock(&vm->lock);
    return NVX_GPUVM_OK;
}
