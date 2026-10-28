# Kernel watchdog service

The watchdog is an opt-in software liveness service driven by PIT ticks. A
client registers a timeout, periodically kicks its handle while making
progress, and checks the latched status from ordinary kernel context.

## Contract

- There are 32 fixed slots; registration does not allocate memory.
- Timeouts are PIT ticks in the range 1 through `0x7fffffff`.
- Handles contain a slot index and generation, so a stale handle cannot
  operate on a later client that reuses the slot.
- Expiry is latched. Kicking an expired handle fails; unregister and register
  again to establish a new monitoring lifetime.
- Deadline comparisons are wrap-safe for the accepted timeout range.
- The timer interrupt uses a nonblocking lock attempt. If another CPU owns the
  registry lock, that tick's scan is skipped rather than spinning in interrupt
  context. Expiry may consequently be observed a tick late under contention.
- The interrupt path does not log, allocate, invoke callbacks, reboot, or
  attempt subsystem recovery. Clients must inspect status and choose a safe
  recovery policy outside interrupt context.
- This is not an x86 hardware watchdog and cannot recover from a CPU that has
  stopped receiving timer interrupts, disabled interrupts indefinitely, or
  deadlocked the machine.

## API sketch

```c
#include "watchdog.h"

kernel_watchdog_handle_t h = kernel_watchdog_register(timeout_ticks);
if (h == 0U) {
    /* No slot or invalid timeout. */
}

/* Call only after measurable progress, not merely from a periodic timer. */
(void)kernel_watchdog_kick(h);

if (kernel_watchdog_status(h) == KERNEL_WATCHDOG_EXPIRED) {
    /* Record/report failure and perform only a subsystem-safe recovery. */
}

(void)kernel_watchdog_unregister(h);
```

No production subsystem is registered as a watchdog client yet. Adding a client
requires a real progress point and a timeout derived from measured worst-case
latency; arbitrary periodic kicks would mask hangs rather than detect them.
The state machine has host regression coverage, but SMP contention and full
kernel behavior still require CI/QEMU evidence.
