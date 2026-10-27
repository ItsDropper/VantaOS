/*
 * Copyright (c) 2026 ItsDropper
 * SPDX-License-Identifier: MIT
 */
#include "ktrace.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/* Host test stub: the serial backend is hardware-specific. */
void serial_write(const char* text)
{
    (void)text;
}

static void test_empty_and_invalid_inputs(void)
{
    ktrace_record_t record;
    assert(ktrace_total_events() == 0U);
    assert(ktrace_snapshot(&record, 1U) == 0U);
    assert(ktrace_snapshot(NULL, 1U) == 0U);
    assert(ktrace_snapshot(&record, 0U) == 0U);
    assert(ktrace_event_count(0U) == 0U);
    assert(ktrace_event_count(KTRACE_EVENT_TYPE_COUNT) == 0U);

    ktrace_emit(0U, 0U, 1U, 2U);
    ktrace_emit(KTRACE_EVENT_TYPE_COUNT, 0U, 1U, 2U);
    assert(ktrace_total_events() == 0U);
}

static void test_payload_and_counters(void)
{
    ktrace_record_t records[4];
    ktrace_emit(KTRACE_EVENT_BOOT_PHASE, 2U, 0x1234U, 0x5678U);
    ktrace_emit(KTRACE_EVENT_PMM_READY, 0U, 4096U, 3072U);

    assert(ktrace_total_events() == 2U);
    assert(ktrace_event_count(KTRACE_EVENT_BOOT_PHASE) == 1U);
    assert(ktrace_event_count(KTRACE_EVENT_PMM_READY) == 1U);
    assert(ktrace_snapshot(records, 4U) == 2U);
    assert(records[0].sequence == 1U);
    assert(records[0].type == KTRACE_EVENT_BOOT_PHASE);
    assert(records[0].cpu_id == 2U);
    assert(records[0].arg0 == 0x1234U);
    assert(records[0].arg1 == 0x5678U);
    assert(records[1].sequence == 2U);
    assert(records[1].type == KTRACE_EVENT_PMM_READY);
    assert(records[1].arg0 == 4096U);
    assert(records[1].arg1 == 3072U);
}

static void test_wrap_and_recent_snapshot(void)
{
    const uint64_t initial = ktrace_total_events();
    const uint64_t iterations = KTRACE_CAPACITY + 37U;
    uint64_t expected_type4 = 0U;

    for (uint64_t i = 0U; i < iterations; ++i)
    {
        uint32_t type = (i & 1U) ? KTRACE_EVENT_SCHED_SWITCH :
                                  KTRACE_EVENT_PCI_SCAN_COMPLETE;
        if (type == KTRACE_EVENT_SCHED_SWITCH)
            ++expected_type4;
        ktrace_emit(type, (uint32_t)(i % 8U), i, ~i);
    }

    assert(ktrace_total_events() == initial + iterations);
    assert(ktrace_event_count(KTRACE_EVENT_SCHED_SWITCH) == expected_type4);
    assert(ktrace_overwritten_estimate() ==
           ktrace_total_events() - KTRACE_CAPACITY);

    ktrace_record_t recent[8];
    size_t count = ktrace_snapshot(recent, 8U);
    assert(count == 8U);
    for (size_t i = 0U; i < count; ++i)
    {
        assert(recent[i].sequence ==
               ktrace_total_events() - 7U + (uint64_t)i);
        assert(recent[i].type == KTRACE_EVENT_SCHED_SWITCH ||
               recent[i].type == KTRACE_EVENT_PCI_SCAN_COMPLETE);
        assert(recent[i].arg1 == ~recent[i].arg0);
    }

    ktrace_record_t ring[KTRACE_CAPACITY];
    count = ktrace_snapshot(ring, KTRACE_CAPACITY);
    assert(count == KTRACE_CAPACITY);
    for (size_t i = 1U; i < count; ++i)
        assert(ring[i - 1U].sequence < ring[i].sequence);
}

int main(void)
{
    test_empty_and_invalid_inputs();
    test_payload_and_counters();
    test_wrap_and_recent_snapshot();
    puts("ktrace tests passed");
    return 0;
}
