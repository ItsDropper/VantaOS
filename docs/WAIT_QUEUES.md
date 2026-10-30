# Kernel process wait queues

`process_wait_queue_t` provides a process-level blocking primitive for event-driven kernel waits. It stores one waiter bit per PID and validates each bit against the process table before waking it. The queue lock serializes the predicate check with waiter registration; process state transitions remain protected by the process-table lock.

## Initialization

Initialize a queue once, before publishing it to producers or consumers. The queue must remain allocated and initialized while any process can be waiting on it.

```c
static process_wait_queue_t event_waiters;

void subsystem_initialize(void)
{
    process_wait_queue_init(&event_waiters);
}
```

## Consumer pattern

The predicate must be atomic. Check it once before taking the queue lock to avoid unnecessary locking, then check it again while holding the queue lock. If it is still false, the locked sleep operation registers the current process and releases the queue lock before blocking.

```c
static volatile uint32_t event_ready;

void wait_for_event(void)
{
    while (__atomic_load_n(&event_ready, __ATOMIC_ACQUIRE) == 0U)
    {
        uint64_t flags = process_wait_queue_lock(&event_waiters);

        if (__atomic_load_n(&event_ready, __ATOMIC_ACQUIRE) != 0U)
        {
            process_wait_queue_unlock(&event_waiters, flags);
            break;
        }

        (void)process_wait_queue_sleep_locked(&event_waiters, flags);
    }
}
```

## Producer pattern

Publish the condition before waking. The release/acquire pair makes the event state visible to a consumer that observes it. Waking is safe whether the consumer has not yet queued, is currently registering, or is already asleep.

```c
void signal_event(void)
{
    __atomic_store_n(&event_ready, 1U, __ATOMIC_RELEASE);
    (void)process_wait_queue_wake_all(&event_waiters);
}
```

Use `process_wait_queue_wake_one()` when one waiter is sufficient. Use `process_wait_queue_wake_all()` for broadcast conditions. Both operations discard stale PID bits and issue reschedule requests after releasing the queue lock.

## Contract and limitations

- Wait queues are for process context, not interrupt handlers that cannot safely take a contended spinlock.
- `process_wait_queue_sleep_locked()` consumes the queue lock and its saved interrupt flags on every return path. Do not unlock the queue again afterward.
- The saved flags must indicate that maskable interrupts were enabled when the queue was locked; otherwise sleeping is rejected.
- The current process must be a normal running process, not the per-CPU idle task.
- Always loop and recheck the predicate after waking. Wakeups may be spurious or another consumer may win the event.
- The predicate must be atomic, and producers must publish it before calling a wake operation.
- A queue must not be destroyed or reinitialized while waiters may still reference it.
- This is a process wait primitive, not yet a general-purpose mutex, semaphore, or condition-variable implementation.