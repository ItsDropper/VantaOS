/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "paging.h"
#include "cpu_local.h"
#include "apic.h"

#include <stdint.h>

#define TLB_SHOOTDOWN_VECTOR 0xF4U
#define TLB_SHOOTDOWN_SPIN_LIMIT 100000000U
static volatile uint32_t tlb_shootdown_lock;
static uint64_t tlb_shootdown_generation;



static inline void invalidate_page(uintptr_t address)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(address) : "memory");
}

/*
 * One outstanding shootdown is allowed globally until every targeted CPU has
 * acknowledged it. This keeps the per-CPU mailbox single-slot and prevents a
 * later request from overwriting an unacknowledged invalidation.
 */
int paging_tlb_shootdown(paging_address_space_t* address_space,
                         uintptr_t virtual_address)
{
    if (!address_space ||
        __atomic_load_n(&address_space->used, __ATOMIC_ACQUIRE) == 0 ||
        (virtual_address & (PAGE_SIZE - 1U)) != 0)
        return 0;

    uint64_t saved_flags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(saved_flags));

    /*
     * Keep interrupts enabled while waiting for the global lock: the current
     * CPU may itself be a target of the lock holder's shootdown. Disable local
     * interrupts only after acquiring the lock, and only restore their prior
     * state when leaving the critical section.
     */
    uint32_t spins = 0;
    while (__atomic_exchange_n(&tlb_shootdown_lock, 1U,
                               __ATOMIC_ACQUIRE) != 0U)
    {
        if (spins++ >= TLB_SHOOTDOWN_SPIN_LIMIT)
            return 0;
        __asm__ volatile ("pause" ::: "memory");
    }
    __asm__ volatile ("cli" ::: "memory");

    cpu_local_t* current = cpu_local_current();
    if (current && current->self == current &&
        __atomic_load_n(&current->current_address_space, __ATOMIC_ACQUIRE) ==
            address_space)
        invalidate_page(virtual_address);

    uint64_t generation = ++tlb_shootdown_generation;
    if (generation == 0U)
        generation = ++tlb_shootdown_generation;

    uint64_t expected[CPU_LOCAL_MAX] = {0};
    int ok = 1;
    for (uint32_t i = 0; i < cpu_local_capacity(); i++)
    {
        cpu_local_t* target = (cpu_local_t*)cpu_local_get(i);
        if (!target || target == current ||
            __atomic_load_n(&target->current_address_space,
                            __ATOMIC_ACQUIRE) != address_space)
            continue;

        __atomic_store_n(&target->tlb_address_space, address_space,
                         __ATOMIC_RELAXED);
        __atomic_store_n(&target->tlb_address, virtual_address,
                         __ATOMIC_RELAXED);
        __atomic_store_n(&target->tlb_request_generation, generation,
                         __ATOMIC_RELAXED);
        __atomic_store_n(&target->tlb_request_pending, 1U,
                         __ATOMIC_RELEASE);

        if (!apic_send_fixed_ipi(target->apic_id, TLB_SHOOTDOWN_VECTOR))
        {
            ok = 0;
            continue;
        }
        expected[i] = generation;
    }

    if (!ok)
    {
        /*
         * Fail closed: an IPI delivery failure means a CPU may retain a stale
         * translation. Keep the mailbox lock held rather than permit another
         * request to overwrite this possibly-undelivered one.
         */
        if (saved_flags & (1ULL << 9))
            __asm__ volatile ("sti" ::: "memory");
        return 0;
    }

    spins = 0;
    for (;;)
    {
        int pending = 0;
        for (uint32_t i = 0; i < cpu_local_capacity(); i++)
        {
            if (expected[i] == 0U)
                continue;
            const cpu_local_t* target = cpu_local_get(i);
            /*
             * Acknowledgements identify one exact serialized request. Ordered
             * comparisons are invalid when the 64-bit generation wraps: an
             * old UINT64_MAX acknowledgement would look newer than generation
             * 1. No later request can be published while this lock is held, so
             * only equality means this target completed the current request.
             */
            if (!target ||
                __atomic_load_n(&target->tlb_ack_generation,
                                __ATOMIC_ACQUIRE) != expected[i])
                pending = 1;
        }

        if (!pending)
            break;
        if (spins++ >= TLB_SHOOTDOWN_SPIN_LIMIT)
        {
            /*
             * Fail closed on a non-responsive target: do not recycle its
             * mailbox while it may still need to acknowledge this request.
             */
            if (saved_flags & (1ULL << 9))
                __asm__ volatile ("sti" ::: "memory");
            return 0;
        }
        __asm__ volatile ("pause" ::: "memory");
    }

    __atomic_store_n(&tlb_shootdown_lock, 0U, __ATOMIC_RELEASE);
    if (saved_flags & (1ULL << 9))
        __asm__ volatile ("sti" ::: "memory");
    return ok;
}

void paging_tlb_shootdown_handle_interrupt(void)
{
    cpu_local_t* local = cpu_local_current();
    if (local && local->self == local && local->online != 0U)
    {
        /*
         * Do not infer whether a request is new by comparing generation
         * numbers: every finite generation counter eventually repeats. The
         * serialized mailbox has an explicit one-shot pending state instead.
         */
        uint32_t pending = __atomic_exchange_n(
            &local->tlb_request_pending, 0U, __ATOMIC_ACQUIRE);
        if (pending != 0U)
        {
            uint64_t generation = __atomic_load_n(
                &local->tlb_request_generation, __ATOMIC_RELAXED);
            paging_address_space_t* target_space = __atomic_load_n(
                &local->tlb_address_space, __ATOMIC_RELAXED);
            uintptr_t address = __atomic_load_n(
                &local->tlb_address, __ATOMIC_RELAXED);

            if (target_space &&
                __atomic_load_n(&local->current_address_space,
                                __ATOMIC_ACQUIRE) == target_space)
                invalidate_page(address);

            __atomic_store_n(&local->tlb_ack_generation, generation,
                             __ATOMIC_RELEASE);
        }
    }
    apic_send_eoi();
}

