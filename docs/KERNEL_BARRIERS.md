# Kernel barriers

`kernel_barrier_t` is a reusable process-context barrier. It releases all participants when the configured number of processes have arrived, then starts a new generation.

## Initialization

```c
static kernel_barrier_t startup_barrier;

if (!kernel_barrier_init(&startup_barrier, 4U))
    return;
```

## Waiting

Every participant calls `kernel_barrier_wait()` exactly once per generation:

```c
if (!kernel_barrier_wait(&startup_barrier))
    return; /* Invalid context or failed wait. */

/* All four participants have now crossed this generation. */
```

The final participant advances the generation and wakes all queued processes. Other participants sleep on the embedded process wait queue and recheck the generation after every wakeup.

## Constraints

- Participants must be normal scheduled processes with maskable interrupts enabled when blocking is required.
- A barrier must be initialized once before concurrent use and must remain alive until all participants have left it.
- All participants must agree on the same threshold and call once per generation. A participant that exits or permanently skips a generation can leave the others blocked indefinitely.
- This primitive is not suitable for interrupt handlers or early-boot single-threaded code.
- The implementation does not provide cancellation, timeout, or owner-death recovery.
