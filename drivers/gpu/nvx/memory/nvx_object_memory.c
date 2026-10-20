/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_object_memory.h"
#include "heap.h"

static void clear_handle(nvx_memory_handle_t *h)
{
    h->token = h->gpu_virtual_address = h->size = 0U;
    h->slot = h->generation = 0U;
}
static int valid_kind(nvx_memory_kind_t k)
{
    return k == NVX_MEMORY_STAGING || k == NVX_MEMORY_FIRMWARE_IMAGE ||
           k == NVX_MEMORY_COMMAND_STORAGE;
}
static const nvx_memory_slot_t *match_handle(const nvx_memory_manager_t *m,
                                              const nvx_memory_handle_t *h)
{
    if (!h || !h->token || h->generation != m->generation ||
        h->slot >= NVX_MEMORY_MAX_OBJECTS)
        return 0;
    const nvx_memory_slot_t *s = &m->slots[h->slot];
    if (s->state != NVX_MEMORY_SLOT_READY || s->token != h->token ||
        s->generation != h->generation ||
        s->mapping.base != h->gpu_virtual_address ||
        s->requested_size != h->size)
        return 0;
    return s;
}
static void release_slot_reservation(nvx_memory_manager_t *m, size_t i)
{
    spinlock_lock(&m->lock);
    m->slots[i].state = NVX_MEMORY_SLOT_FREE;
    m->slots[i].token = m->slots[i].requested_size = 0U;
    m->slots[i].generation = 0U;
    m->slots[i].kind = 0U;
    spinlock_unlock(&m->lock);
}

nvx_memory_status_t nvx_memory_init(nvx_memory_manager_t *m,
                                    nvx_gpuvm_t *gpuvm, unsigned address_bits)
{
    if (!m || !gpuvm) return NVX_MEMORY_INVALID_ARGUMENT;
    if (m->initialized) return NVX_MEMORY_INVALID_ARGUMENT;
    if (!gpuvm->initialized || gpuvm->faulted)
        return NVX_MEMORY_ADDRESS_SPACE_FAILED;
    if (address_bits != 32U && address_bits != 64U)
        return NVX_MEMORY_INVALID_ARGUMENT;
    spinlock_init(&m->lock);
    m->gpuvm = gpuvm;
    m->next_token = 1U;
    if (++m->generation == 0U) m->generation = 1U;
    m->address_bits = address_bits;
    for (size_t i = 0; i < NVX_MEMORY_MAX_OBJECTS; ++i) {
        m->slots[i].state = NVX_MEMORY_SLOT_FREE;
        m->slots[i].token = m->slots[i].requested_size = 0U;
        m->slots[i].generation = 0U;
        m->slots[i].kind = 0U;
        m->slots[i].mapping.valid = 0U;
        m->slots[i].dma.acquired = 0U;
        m->slots[i].dma.page_count = 0U;
    }
    m->initialized = 1U;
    return NVX_MEMORY_OK;
}

nvx_memory_status_t nvx_memory_alloc(nvx_memory_manager_t *m, size_t size,
                                     nvx_memory_kind_t kind,
                                     nvx_memory_handle_t *out)
{
    size_t index = NVX_MEMORY_MAX_OBJECTS;
    uint64_t token;
    nvx_gpuvm_mapping_t mapping = {0};
    nvx_dma_buffer_t dma = {0};
    uint64_t *physical = 0;
    nvx_gpuvm_status_t vs;
    if (!m || !out) return NVX_MEMORY_INVALID_ARGUMENT;
    clear_handle(out);
    if (!m->initialized) return NVX_MEMORY_NOT_INITIALIZED;
    if (!size || size > NVX_DMA_MAX_BYTES || !valid_kind(kind))
        return NVX_MEMORY_INVALID_ARGUMENT;

    spinlock_lock(&m->lock);
    if (!m->next_token || m->next_token == UINT64_MAX) {
        spinlock_unlock(&m->lock);
        return NVX_MEMORY_CORRUPT_STATE;
    }
    for (size_t i = 0; i < NVX_MEMORY_MAX_OBJECTS; ++i) {
        if (m->slots[i].state == NVX_MEMORY_SLOT_FREE) {
            index = i;
            m->slots[i].state = NVX_MEMORY_SLOT_RESERVED;
            m->slots[i].token = m->next_token++;
            m->slots[i].generation = m->generation;
            m->slots[i].requested_size = size;
            m->slots[i].kind = (uint8_t)kind;
            break;
        }
    }
    if (index == NVX_MEMORY_MAX_OBJECTS) {
        spinlock_unlock(&m->lock);
        return NVX_MEMORY_OUT_OF_OBJECTS;
    }
    token = m->slots[index].token;
    spinlock_unlock(&m->lock);

    vs = nvx_gpuvm_reserve(m->gpuvm, size, NVX_GPUVM_PAGE_SIZE, 1U, &mapping);
    if (vs != NVX_GPUVM_OK) {
        release_slot_reservation(m, index);
        return vs == NVX_GPUVM_BUSY ? NVX_MEMORY_BUSY :
                                      NVX_MEMORY_ADDRESS_SPACE_FAILED;
    }
    nvx_dma_status_t ds = nvx_dma_alloc(size, m->address_bits, &dma);
    if (ds != NVX_DMA_OK) {
        (void)nvx_gpuvm_release(m->gpuvm, &mapping);
        release_slot_reservation(m, index);
        return NVX_MEMORY_ALLOCATION_FAILED;
    }
    if (dma.page_count > SIZE_MAX / sizeof(*physical)) {
        nvx_dma_release(&dma);
        (void)nvx_gpuvm_release(m->gpuvm, &mapping);
        release_slot_reservation(m, index);
        return NVX_MEMORY_ALLOCATION_FAILED;
    }
    physical = kmalloc(dma.page_count * sizeof(*physical));
    if (!physical) {
        nvx_dma_release(&dma);
        (void)nvx_gpuvm_release(m->gpuvm, &mapping);
        release_slot_reservation(m, index);
        return NVX_MEMORY_ALLOCATION_FAILED;
    }
    for (size_t i = 0; i < dma.page_count; ++i)
        physical[i] = dma.pages[i].physical_address;

    vs = nvx_gpuvm_map_pages(m->gpuvm, &mapping, physical, dma.page_count,
                             NVX_GPUVM_ACCESS_READ | NVX_GPUVM_ACCESS_WRITE);
    kfree(physical);
    physical = 0;
    if (vs != NVX_GPUVM_OK) {
        if (mapping.mapped || m->gpuvm->faulted) {
            /* A failed TLB flush may retain stale translations: never free RAM. */
            spinlock_lock(&m->lock);
            m->slots[index].dma = dma;
            m->slots[index].mapping = mapping;
            m->slots[index].state = NVX_MEMORY_SLOT_QUARANTINED;
            spinlock_unlock(&m->lock);
            return NVX_MEMORY_GPUVM_FAILED;
        }
        (void)nvx_gpuvm_release(m->gpuvm, &mapping);
        nvx_dma_release(&dma);
        release_slot_reservation(m, index);
        return vs == NVX_GPUVM_BUSY ? NVX_MEMORY_BUSY : NVX_MEMORY_GPUVM_FAILED;
    }

    spinlock_lock(&m->lock);
    if (m->slots[index].state != NVX_MEMORY_SLOT_RESERVED ||
        m->slots[index].token != token) {
        spinlock_unlock(&m->lock);
        /* Ownership corruption: intentionally retain the DMA pages. */
        return NVX_MEMORY_CORRUPT_STATE;
    }
    m->slots[index].mapping = mapping;
    m->slots[index].dma = dma;
    m->slots[index].state = NVX_MEMORY_SLOT_READY;
    out->token = token;
    out->gpu_virtual_address = mapping.base;
    out->size = size;
    out->slot = (uint32_t)index;
    out->generation = m->generation;
    spinlock_unlock(&m->lock);
    return NVX_MEMORY_OK;
}

nvx_memory_status_t nvx_memory_get_info(nvx_memory_manager_t *m,
                                        const nvx_memory_handle_t *h,
                                        nvx_memory_info_t *out)
{
    if (out) *out = (nvx_memory_info_t){0};
    if (!m || !h || !out) return NVX_MEMORY_INVALID_ARGUMENT;
    if (!m->initialized) return NVX_MEMORY_NOT_INITIALIZED;
    spinlock_lock(&m->lock);
    const nvx_memory_slot_t *s = match_handle(m, h);
    if (!s) {
        spinlock_unlock(&m->lock);
        return NVX_MEMORY_STALE_HANDLE;
    }
    out->cpu_address = s->dma.cpu_address;
    out->gpu_virtual_address = s->mapping.base;
    out->size = s->requested_size;
    out->kind = (nvx_memory_kind_t)s->kind;
    spinlock_unlock(&m->lock);
    return NVX_MEMORY_OK;
}

nvx_memory_status_t nvx_memory_cpu_address(nvx_memory_manager_t *m,
                                           const nvx_memory_handle_t *h,
                                           void **out)
{
    if (out) *out = 0;
    if (!out) return NVX_MEMORY_INVALID_ARGUMENT;
    nvx_memory_info_t info;
    nvx_memory_status_t s = nvx_memory_get_info(m, h, &info);
    if (s == NVX_MEMORY_OK) *out = info.cpu_address;
    return s;
}

nvx_memory_status_t nvx_memory_release(nvx_memory_manager_t *m,
                                       nvx_memory_handle_t *h)
{
    if (!m || !h) return NVX_MEMORY_INVALID_ARGUMENT;
    if (!m->initialized) return NVX_MEMORY_NOT_INITIALIZED;
    spinlock_lock(&m->lock);
    if (!match_handle(m, h)) {
        spinlock_unlock(&m->lock);
        return NVX_MEMORY_STALE_HANDLE;
    }
    nvx_memory_slot_t *slot = &m->slots[h->slot];
    slot->state = NVX_MEMORY_SLOT_RELEASING;
    nvx_gpuvm_mapping_t *mapping = &slot->mapping;
    nvx_dma_buffer_t *dma = &slot->dma;
    spinlock_unlock(&m->lock);

    nvx_gpuvm_status_t vs = nvx_gpuvm_release(m->gpuvm, mapping);
    if (vs != NVX_GPUVM_OK) {
        spinlock_lock(&m->lock);
        slot->state = vs == NVX_GPUVM_BUSY ? NVX_MEMORY_SLOT_READY :
                                             NVX_MEMORY_SLOT_QUARANTINED;
        spinlock_unlock(&m->lock);
        return vs == NVX_GPUVM_BUSY ? NVX_MEMORY_BUSY : NVX_MEMORY_GPUVM_FAILED;
    }
    nvx_dma_release(dma);
    spinlock_lock(&m->lock);
    slot->token = slot->requested_size = 0U;
    slot->generation = 0U;
    slot->kind = 0U;
    slot->state = NVX_MEMORY_SLOT_FREE;
    spinlock_unlock(&m->lock);
    clear_handle(h);
    return NVX_MEMORY_OK;
}

nvx_memory_status_t nvx_memory_validate(nvx_memory_manager_t *m)
{
    size_t quarantined = 0U;
    if (!m) return NVX_MEMORY_INVALID_ARGUMENT;
    if (!m->initialized) return NVX_MEMORY_NOT_INITIALIZED;
    spinlock_lock(&m->lock);
    for (size_t i = 0; i < NVX_MEMORY_MAX_OBJECTS; ++i) {
        const nvx_memory_slot_t *s = &m->slots[i];
        if (s->state == NVX_MEMORY_SLOT_READY) {
            if (!s->token || s->generation != m->generation ||
                !s->mapping.valid || !s->mapping.mapped ||
                !s->dma.acquired || !s->requested_size ||
                s->dma.size != s->requested_size) {
                spinlock_unlock(&m->lock);
                return NVX_MEMORY_CORRUPT_STATE;
            }
        } else if (s->state == NVX_MEMORY_SLOT_RESERVED ||
                   s->state == NVX_MEMORY_SLOT_RELEASING) {
            continue;
        } else if (s->state == NVX_MEMORY_SLOT_QUARANTINED) {
            ++quarantined;
            if (!s->token || !s->dma.acquired || !s->mapping.valid) {
                spinlock_unlock(&m->lock);
                return NVX_MEMORY_CORRUPT_STATE;
            }
        } else if (s->state != NVX_MEMORY_SLOT_FREE) {
            spinlock_unlock(&m->lock);
            return NVX_MEMORY_CORRUPT_STATE;
        }
    }
    spinlock_unlock(&m->lock);
    if (nvx_gpuvm_validate(m->gpuvm) != NVX_GPUVM_OK)
        return NVX_MEMORY_CORRUPT_STATE;
    return quarantined ? NVX_MEMORY_GPUVM_FAILED : NVX_MEMORY_OK;
}
