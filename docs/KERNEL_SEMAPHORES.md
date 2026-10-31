# Kernel semaphores

`kernel_semaphore_t` is a bounded counting semaphore built on the process wait-queue primitive. The internal queue lock protects both the count and waiter registration, preventing a post from being lost between checking the count and blocking.

## Initialization

```c
static kernel_semaphore_t io_slots;

void io_initialize(void)
{
    /* Initially two operations may run concurrently; capacity is four. */
    (void)kernel_semaphore_init(&io_slots, 2U, 4U);
}
```

The initializer rejects a null object, a zero maximum, or an initial count greater than the maximum.

## Waiting and posting

```c
if (!kernel_semaphore_wait(&io_slots))
    return; /* No valid scheduled process context. */

perform_io();
(void)kernel_semaphore_post(&io_slots);
```

`kernel_semaphore_try_wait()` consumes a token without blocking. `kernel_semaphore_post()` adds one token and wakes one waiter; it fails rather than exceeding the configured maximum.

## Deadline-based wait

`kernel_semaphore_wait_until(semaphore, deadline_ticks)` waits until a permit is available or the absolute monotonic PIT-tick deadline expires. A permit observed while holding the semaphore's queue lock takes precedence over timeout.

```c
uint64_t deadline = timer_get_ticks64() + 250U;
if (!kernel_semaphore_wait_until(&io_slots, deadline))
    handle_io_capacity_timeout();
```

The deadline is expressed in PIT ticks, not milliseconds. Derive it from `timer_get_ticks64()` and the configured timer frequency. Timed waits require the same scheduled process context and enabled maskable interrupts as ordinary blocking waits.

## Constraints

- Blocking waits require a normal scheduled process with maskable interrupts enabled.
- Initialize once before concurrent use; do not reinitialize while waiters may exist.
- The implementation is not FIFO-fair and does not provide priority inheritance.
- Keep the semaphore's storage alive while processes may be waiting on it.
- This primitive is not an interrupt completion queue. Use a dedicated IRQ-safe deferred-work path for interrupt-driven producers.
- Every successful wait consumes exactly one token, which should be returned with a post when the protected resource becomes available.