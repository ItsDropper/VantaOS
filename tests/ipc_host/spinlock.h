#ifndef VANTAOS_IPC_HOST_SPINLOCK_H
#define VANTAOS_IPC_HOST_SPINLOCK_H
#include <stdint.h>
typedef struct { int unused; } spinlock_t;
#define SPINLOCK_INITIALIZER { 0 }
static inline uint64_t spinlock_lock_irqsave(spinlock_t *lock) { (void)lock; return 0; }
static inline void spinlock_unlock_irqrestore(spinlock_t *lock, uint64_t flags) { (void)lock; (void)flags; }
#endif
