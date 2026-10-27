/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "ktrace.h"

/*
 * Keep every shared field atomic. A reader can inspect a record while another
 * CPU overwrites its slot, so plain struct copies would introduce data races.
 * The published sequence is the commit marker and is checked before and after
 * the payload is sampled.
 */
typedef struct {
    volatile uint64_t committed_sequence;
    volatile uint32_t type;
    volatile uint32_t cpu_id;
    volatile uint64_t arg0;
    volatile uint64_t arg1;
} ktrace_slot_t;

static ktrace_slot_t trace_ring[KTRACE_CAPACITY];
static volatile uint64_t next_sequence;
static volatile uint64_t event_counts[KTRACE_EVENT_TYPE_COUNT];

static uint64_t atomic_load_u64(const volatile uint64_t* value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

void ktrace_emit(uint32_t type, uint32_t cpu_id, uint64_t arg0, uint64_t arg1)
{
    if (type == 0U || type >= KTRACE_EVENT_TYPE_COUNT)
        return;

    uint64_t sequence =
        __atomic_add_fetch(&next_sequence, 1U, __ATOMIC_RELAXED);
    ktrace_slot_t* slot = &trace_ring[(sequence - 1U) % KTRACE_CAPACITY];

    __atomic_fetch_add(&event_counts[type], 1U, __ATOMIC_RELAXED);
    __atomic_store_n(&slot->type, type, __ATOMIC_RELAXED);
    __atomic_store_n(&slot->cpu_id, cpu_id, __ATOMIC_RELAXED);
    __atomic_store_n(&slot->arg0, arg0, __ATOMIC_RELAXED);
    __atomic_store_n(&slot->arg1, arg1, __ATOMIC_RELAXED);
    __atomic_store_n(&slot->committed_sequence, sequence, __ATOMIC_RELEASE);
}

size_t ktrace_snapshot(ktrace_record_t* output, size_t capacity)
{
    if (output == NULL || capacity == 0U)
        return 0U;

    uint64_t end = atomic_load_u64(&next_sequence);
    if (end == 0U)
        return 0U;

    uint64_t available = end < KTRACE_CAPACITY ? end : KTRACE_CAPACITY;
    size_t requested = capacity < available ? capacity : (size_t)available;
    uint64_t first = end - requested + 1U;
    size_t copied = 0U;

    for (uint64_t sequence = first; sequence <= end; ++sequence)
    {
        ktrace_slot_t* slot =
            &trace_ring[(sequence - 1U) % KTRACE_CAPACITY];
        uint64_t before = atomic_load_u64(&slot->committed_sequence);
        if (before != sequence)
            continue;

        ktrace_record_t record;
        record.sequence = sequence;
        record.type = __atomic_load_n(&slot->type, __ATOMIC_RELAXED);
        record.cpu_id = __atomic_load_n(&slot->cpu_id, __ATOMIC_RELAXED);
        record.arg0 = __atomic_load_n(&slot->arg0, __ATOMIC_RELAXED);
        record.arg1 = __atomic_load_n(&slot->arg1, __ATOMIC_RELAXED);

        uint64_t after = atomic_load_u64(&slot->committed_sequence);
        if (after != before)
            continue;
        output[copied++] = record;
    }

    return copied;
}

uint64_t ktrace_event_count(uint32_t type)
{
    if (type == 0U || type >= KTRACE_EVENT_TYPE_COUNT)
        return 0U;
    return atomic_load_u64(&event_counts[type]);
}

uint64_t ktrace_total_events(void)
{
    return atomic_load_u64(&next_sequence);
}

uint64_t ktrace_overwritten_estimate(void)
{
    uint64_t total = atomic_load_u64(&next_sequence);
    return total > KTRACE_CAPACITY ? total - KTRACE_CAPACITY : 0U;
}
