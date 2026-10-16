#ifndef CPU_LOCAL_H
#define CPU_LOCAL_H

#include <stdint.h>

struct paging_address_space;

#define CPU_LOCAL_MAX 32U

typedef struct cpu_local
{
    /* Must remain first: cpu_local_current reads this through GS:0. */
    struct cpu_local* self;
    uint32_t logical_id;
    uint32_t apic_id;
    volatile uint32_t online;
    volatile uint32_t current_pid;
    volatile uint32_t interrupt_depth;
    uint32_t reserved;
    /* Updated only by this logical CPU; also validates GS-based lookup. */
    volatile uint64_t work_iterations;
    /* Per-CPU scheduler quantum; never shared between logical CPUs. */
    volatile uint32_t scheduler_ticks;
    /* Incremented only by this CPU's local APIC timer interrupt. */
    volatile uint32_t lapic_timer_ticks;
    /* Number of reschedule IPIs delivered to this CPU (not scheduled yet). */
    volatile uint32_t reschedule_ipi_ticks;
    /* Address space loaded in this CPU's CR3 (not a global singleton). */
    struct paging_address_space* current_address_space;
    /* Single-slot synchronous TLB shootdown mailbox; globally serialized. */
    volatile uint64_t tlb_request_generation;
    volatile uint64_t tlb_ack_generation;
    struct paging_address_space* volatile tlb_address_space;
    volatile uintptr_t tlb_address;
} cpu_local_t;

/* Installs the current CPU's GS base and initializes its private state. */
int cpu_local_initialize(uint32_t logical_id, uint32_t apic_id);
cpu_local_t* cpu_local_current(void);
const cpu_local_t* cpu_local_get(uint32_t logical_id);
unsigned int cpu_local_capacity(void);

#endif
