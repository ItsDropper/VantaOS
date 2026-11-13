# Kernel watchdog service

The watchdog is an opt-in software liveness service driven by PIT ticks. A
client registers a timeout, periodically kicks its handle while making
progress, and checks the latched status from ordinary kernel context.

## Contract

- There are 32 fixed slots; registration does not allocate memory.
- Timeouts are PIT ticks in the range 1 through `0x7fffffff`.
- Handles are 64-bit values containing an 8-bit slot index and a 56-bit
  generation, so stale handles are rejected across slot reuse unless the
  generation counter eventually wraps.
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

## Current production client

The desktop compositor registers a 500-tick (5-second at the configured
100 Hz PIT rate) watchdog after entering its main process. It kicks the handle
only after completing a desktop iteration. A separate `desktop-watchdog` kernel
process checks the latched status and emits
`VANTAOS_DESKTOP_WATCHDOG_EXPIRED` if the compositor stops making progress.
When the desktop heartbeat resumes, the observer reports recovery, replaces
the expired handle, and the compositor resumes kicking the new handle.

This is deliberately report-only: the observer does not terminate or restart
the compositor because its GUI state and owned windows do not yet have a safe
restart contract. It detects a blocked desktop task only while PIT interrupts
and the scheduler continue to run. It cannot detect or recover a whole-machine
lockup or a lost timer interrupt; that requires an independent hardware
watchdog or another execution domain.

The device-I/O worker is separate from the compositor and performs ACPI/I2C
initialization plus USB and I2C HID polling. This keeps bounded but potentially
slow bus transactions out of the frame/input task; the desktop watchdog is
intentionally scoped to compositor progress rather than peripheral progress.

## Production-client acceptance criteria

Before registering a client, document all of the following:

1. The exact operation that constitutes forward progress (not merely a timer tick).
2. A timeout derived from a measured or bounded worst-case duration at the
   configured PIT frequency.
3. Where expiry is observed outside the timer interrupt, including how that
   observer remains runnable if the monitored component stops progressing.
4. A recovery action that is safe for the affected subsystem and does not run
   while holding the watchdog registry lock or from interrupt context.
5. A regression test for normal progress, a deliberately withheld heartbeat,
   expiry observation, and recovery/reporting behavior.

A scheduler heartbeat alone is not sufficient evidence of system liveness: if
its producer and observer share the same blocked execution path, a stalled CPU
may never report its own expiry. The state machine has host regression coverage,
but SMP contention and full kernel behavior still require CI/QEMU evidence.
