#include "cpu_local.h"
#include "paging.h"

#include <stddef.h>
#include <stdint.h>

#define IA32_GS_BASE_MSR 0xC0000101U

static cpu_local_t cpu_locals[CPU_LOCAL_MAX];

static uint64_t read_msr(uint32_t msr)
{
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value)
{
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

int cpu_local_initialize(uint32_t logical_id, uint32_t apic_id)
{
    if (logical_id >= CPU_LOCAL_MAX)
        return 0;

    cpu_local_t* local = &cpu_locals[logical_id];
    local->self = local;
    local->logical_id = logical_id;
    local->apic_id = apic_id;
    local->current_pid = 0;
    local->interrupt_depth = 0;
    local->work_iterations = 0;
    local->scheduler_ticks = 0;
    local->lapic_timer_ticks = 0;
    local->reschedule_ipi_ticks = 0;
    local->current_address_space = paging_get_kernel_address_space();
    local->tlb_request_generation = 0;
    local->tlb_ack_generation = 0;
    local->tlb_address_space = NULL;
    local->tlb_address = 0;
    __atomic_store_n(&local->online, 1U, __ATOMIC_RELEASE);

    /*
     * Each CPU has its own IA32_GS_BASE MSR. No SWAPGS is needed yet because
     * VantaOS has not entered user mode; a future ring-3 transition must add
     * a deliberate kernel/user GS-base protocol before exposing this API.
     */
    write_msr(IA32_GS_BASE_MSR, (uint64_t)(uintptr_t)local);
    return cpu_local_current() == local;
}

cpu_local_t* cpu_local_current(void)
{
    /* GS is not guaranteed to be initialized when APIC/SMP is unavailable. */
    if (read_msr(IA32_GS_BASE_MSR) == 0)
        return NULL;

    cpu_local_t* local;
    __asm__ volatile ("movq %%gs:0, %0" : "=r"(local));
    return local;
}

const cpu_local_t* cpu_local_get(uint32_t logical_id)
{
    if (logical_id >= CPU_LOCAL_MAX ||
        __atomic_load_n(&cpu_locals[logical_id].online,
                        __ATOMIC_ACQUIRE) == 0U)
        return NULL;
    return &cpu_locals[logical_id];
}

unsigned int cpu_local_capacity(void)
{
    return CPU_LOCAL_MAX;
}
