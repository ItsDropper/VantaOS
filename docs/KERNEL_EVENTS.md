# Kernel events

`kernel_event_t` provides a persistent process-context notification backed by the process wait-queue protocol.

## Manual-reset event

A manual-reset event remains signaled until explicitly reset. Setting it wakes every current waiter; later waiters pass immediately until reset.

```c
static kernel_event_t device_ready = KERNEL_EVENT_INITIALIZER(1, 0);

void device_publish_ready(void)
{
    (void)kernel_event_set(&device_ready);
}

void wait_for_device(void)
{
    (void)kernel_event_wait(&device_ready);
}
```

## Auto-reset event

An auto-reset event releases one waiter per set operation. Its signal remains pending if no waiter is registered, and one successful wait consumes it.

```c
static kernel_event_t work_available = KERNEL_EVENT_INITIALIZER(0, 0);

void submit_work(void)
{
    /* Publish work before signaling. */
    (void)kernel_event_set(&work_available);
}

void worker(void)
{
    while (kernel_event_wait(&work_available))
        process_one_work_item();
}
```

## Contract

- Initialize once before concurrent use; do not reset or destroy storage while waiters may reference it.
- Every operation, including set/reset/is-set, is process-context only. The queue lock can be held by a process interrupted on this CPU, so calling even a nonblocking event operation from an ISR can deadlock.
- Blocking waits require a normal scheduled process context with maskable interrupts enabled.
- Always publish the associated data before setting the event. Use the event as notification, not as a replacement for protecting the data itself.
- Auto-reset events are not FIFO-fair. Repeated sets coalesce into one pending signal; they are not counting semaphores.
- Manual-reset events wake all waiters and remain set until reset.
- Do not call the blocking wait from interrupt context.
