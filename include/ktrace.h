/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#ifndef VANTA_KTRACE_H
#define VANTA_KTRACE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Bounded, allocation-free kernel event history.
 *
 * Writers never acquire a lock and may be called from interrupt context.
 * Records can be overwritten under pressure; readers must treat snapshots as
 * best-effort diagnostics, not as a durable audit log.
 */
#define KTRACE_CAPACITY 1024U
#define KTRACE_EVENT_TYPE_COUNT 64U
#define KTRACE_CPU_UNKNOWN 0xFFFFFFFFU

typedef enum {
    KTRACE_EVENT_BOOT_PHASE = 1U,
    KTRACE_EVENT_PMM_READY = 2U,
    KTRACE_EVENT_PMM_ALLOC_FAILURE = 3U,
    KTRACE_EVENT_SCHED_SWITCH = 4U,
    KTRACE_EVENT_PCI_SCAN_COMPLETE = 5U,
    KTRACE_EVENT_PANIC = 6U,
    KTRACE_EVENT_DRIVER_INIT = 7U,
    KTRACE_EVENT_MEMORY_RESERVE = 8U,
    KTRACE_EVENT_USER_FAULT = 9U,
    KTRACE_EVENT_MAX_DEFINED = 10U
} ktrace_event_type_t;

typedef struct {
    uint64_t sequence;
    uint32_t type;
    uint32_t cpu_id;
    uint64_t arg0;
    uint64_t arg1;
} ktrace_record_t;

void ktrace_emit(uint32_t type, uint32_t cpu_id, uint64_t arg0, uint64_t arg1);
size_t ktrace_snapshot(ktrace_record_t* output, size_t capacity);
uint64_t ktrace_event_count(uint32_t type);
uint64_t ktrace_total_events(void);
uint64_t ktrace_overwritten_estimate(void);

#endif
