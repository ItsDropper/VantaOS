# Kernel mutexes

`kernel_mutex_t` is a sleeping, non-recursive mutex built on the process wait-queue primitive. It is suitable for protecting short-to-medium kernel critical sections that may contend between normal process contexts.

## Initialization

Use `kernel_mutex_init()` before publishing the mutex, or the static initializer for a statically allocated mutex.

```c
static kernel_mutex_t device_mutex = KERNEL_MUTEX_INITIALIZER;

/* For dynamically allocated storage: */
kernel_mutex_t* mutex = obtain_mutex_storage();
kernel_mutex_init(mutex);
```

## Locking

```c
if (!kernel_mutex_lock(&device_mutex))
    return; /* Invalid context or recursive acquisition. */

/* Protected shared state. */

(void)kernel_mutex_unlock(&device_mutex);
```

`kernel_mutex_try_lock()` never sleeps and returns zero if the mutex is already owned. Unlock is restricted to the owning process. Contended `kernel_mutex_lock()` requires a normal scheduled process with maskable interrupts enabled; it blocks through the wait queue and retries ownership after every wakeup.

## Current guarantees and limitations

- Ownership transitions are serialized by the embedded wait-queue lock.
- The wait-queue predicate protocol prevents a lost wake between observing contention and registering the current process.
- Mutexes are non-recursive. Attempting to lock a mutex already owned by the current process fails rather than deadlocking.
- The implementation is not priority-inheritance or FIFO-fair; a newly arriving contender can acquire the mutex before a woken waiter.
- Mutexes must not be used from interrupt context. Use a spinlock or an IRQ-safe primitive for interrupt-shared state.
- Owner death is not recovered automatically. The owning process must release the mutex before termination; robust owner cleanup is future work.
- Initialize each mutex exactly once before concurrent use. Do not reinitialize a locked mutex or one with waiters.

Do not hold a mutex while waiting for unrelated external events or performing unbounded I/O. Keep the protected region bounded and define a consistent lock order when multiple mutexes are involved.