/*
 * Copyright (c) 2026 ItsDropper
 * SPDX-License-Identifier: MIT
 */
#include "ktrace.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>

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


typedef struct {
    uint32_t cpu_id;
    uint32_t iterations;
} writer_args_t;

static void* concurrent_writer(void* opaque)
{
    const writer_args_t* args = (const writer_args_t*)opaque;
    for (uint32_t i = 0U; i < args->iterations; ++i)
    {
        uint64_t value = ((uint64_t)args->cpu_id << 32U) | i;
        uint32_t type = (i & 1U) ? KTRACE_EVENT_SCHED_SWITCH :
                                  KTRACE_EVENT_PCI_SCAN_COMPLETE;
        ktrace_emit(type, args->cpu_id, value, ~value);
    }
    return NULL;
}

static void test_concurrent_writers_and_readers(void)
{
    enum { THREAD_COUNT = 4, ITERATIONS = 5000 };
    pthread_t threads[THREAD_COUNT];
    writer_args_t args[THREAD_COUNT];

    for (uint32_t i = 0U; i < THREAD_COUNT; ++i)
    {
        args[i].cpu_id = i;
        args[i].iterations = ITERATIONS;
        assert(pthread_create(&threads[i], NULL, concurrent_writer,
                              &args[i]) == 0);
    }

    for (uint32_t round = 0U; round < 100U; ++round)
    {
        ktrace_record_t snapshot[16];
        size_t count = ktrace_snapshot(snapshot, 16U);
        for (size_t i = 0U; i < count; ++i)
        {
            assert(snapshot[i].cpu_id < THREAD_COUNT);
            assert(snapshot[i].arg1 == ~snapshot[i].arg0);
            assert(snapshot[i].type == KTRACE_EVENT_SCHED_SWITCH ||
                   snapshot[i].type == KTRACE_EVENT_PCI_SCAN_COMPLETE);
        }
    }

    for (uint32_t i = 0U; i < THREAD_COUNT; ++i)
        assert(pthread_join(threads[i], NULL) == 0);

    ktrace_record_t snapshot[64];
    size_t count = ktrace_snapshot(snapshot, 64U);
    for (size_t i = 0U; i < count; ++i)
    {
        assert(snapshot[i].cpu_id < THREAD_COUNT);
        assert(snapshot[i].arg1 == ~snapshot[i].arg0);
        assert(snapshot[i].type == KTRACE_EVENT_SCHED_SWITCH ||
               snapshot[i].type == KTRACE_EVENT_PCI_SCAN_COMPLETE);
        if (i != 0U)
            assert(snapshot[i - 1U].sequence < snapshot[i].sequence);
    }
}

int main(void)
{
    test_empty_and_invalid_inputs();
    test_payload_and_counters();
    test_wrap_and_recent_snapshot();
    test_concurrent_writers_and_readers();
    puts("ktrace tests passed");
    return 0;
}
