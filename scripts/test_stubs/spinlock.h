/*
 * Host-test shim only. The kernel uses include/spinlock.h.
 * Single-threaded watchdog tests do not exercise SMP lock behavior.
 */
#ifndef VANTAOS_TEST_SPINLOCK_H
#define VANTAOS_TEST_SPINLOCK_H

#include <stdint.h>

typedef struct { uint32_t unused; } spinlock_t;
#define SPINLOCK_INITIALIZER { 0U }

static inline uint64_t spinlock_lock_irqsave(spinlock_t* lock)
{
    (void)lock;
    return 0U;
}
static inline void spinlock_unlock_irqrestore(spinlock_t* lock, uint64_t flags)
{
    (void)lock;
    (void)flags;
}
static inline int spinlock_try_lock(spinlock_t* lock)
{
    (void)lock;
    return 1;
}
static inline void spinlock_unlock(spinlock_t* lock)
{
    (void)lock;
}

#endif
