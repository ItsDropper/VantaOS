/*
 * Host-test shim only. The kernel uses include/spinlock.h.
 * Unlike scripts/test_stubs/spinlock.h this one is a real ticket lock so
 * multi-threaded host tests exercise genuine mutual exclusion. Interrupt
 * state is not touched because `cli` is privileged outside the kernel.
 */
#ifndef VANTAOS_TEST_SMP_SPINLOCK_H
#define VANTAOS_TEST_SMP_SPINLOCK_H

#include <sched.h>
#include <stdint.h>

typedef struct
{
    volatile uint32_t next_ticket;
    volatile uint32_t owner_ticket;
} spinlock_t;

#define SPINLOCK_INITIALIZER { 0U, 0U }

static inline void spinlock_lock(spinlock_t* lock)
{
    uint32_t ticket = __atomic_fetch_add(&lock->next_ticket, 1U,
                                         __ATOMIC_RELAXED);
    /* Yield while waiting: a FIFO ticket lock stalls badly if the host has
       fewer cores than test threads and the next ticket holder is descheduled. */
    while (__atomic_load_n(&lock->owner_ticket, __ATOMIC_ACQUIRE) != ticket)
        sched_yield();
}

static inline void spinlock_unlock(spinlock_t* lock)
{
    __atomic_fetch_add(&lock->owner_ticket, 1U, __ATOMIC_RELEASE);
}

static inline uint64_t spinlock_lock_irqsave(spinlock_t* lock)
{
    spinlock_lock(lock);
    return 0U;
}

static inline void spinlock_unlock_irqrestore(spinlock_t* lock, uint64_t flags)
{
    (void)flags;
    spinlock_unlock(lock);
}

#endif
