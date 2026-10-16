#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

/*
 * FIFO ticket spinlock for short kernel critical sections.
 *
 * This lock provides mutual exclusion and acquire/release ordering, not
 * sleeping or owner-death recovery. Callers must not block, yield, or enter
 * code that can recursively acquire the same lock while holding it.
 */
typedef struct
{
    volatile uint32_t next_ticket;
    volatile uint32_t owner_ticket;
} spinlock_t;

#define SPINLOCK_INITIALIZER { 0U, 0U }

static inline void spinlock_init(spinlock_t* lock)
{
    __atomic_store_n(&lock->owner_ticket, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&lock->next_ticket, 0U, __ATOMIC_RELAXED);
}

static inline void spinlock_lock(spinlock_t* lock)
{
    uint32_t ticket = __atomic_fetch_add(&lock->next_ticket, 1U,
                                         __ATOMIC_RELAXED);
    while (__atomic_load_n(&lock->owner_ticket, __ATOMIC_ACQUIRE) != ticket)
        __asm__ volatile ("pause" ::: "memory");
}

static inline int spinlock_try_lock(spinlock_t* lock)
{
    uint32_t owner = __atomic_load_n(&lock->owner_ticket, __ATOMIC_ACQUIRE);
    uint32_t expected = owner;
    return __atomic_compare_exchange_n(&lock->next_ticket, &expected,
                                       owner + 1U, 0,
                                       __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static inline void spinlock_unlock(spinlock_t* lock)
{
    __atomic_fetch_add(&lock->owner_ticket, 1U, __ATOMIC_RELEASE);
}

/* Disable local maskable interrupts before waiting to avoid local re-entry. */
static inline uint64_t spinlock_lock_irqsave(spinlock_t* lock)
{
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli"
                      : "=r"(flags) :: "memory", "cc");
    spinlock_lock(lock);
    return flags;
}

static inline void spinlock_unlock_irqrestore(spinlock_t* lock,
                                               uint64_t flags)
{
    spinlock_unlock(lock);
    __asm__ volatile ("pushq %0; popfq"
                      :: "r"(flags) : "memory", "cc");
}

#endif
