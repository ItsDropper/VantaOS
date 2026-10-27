# Kernel event tracing

VantaOS has a small structured trace ring for diagnosing failures that cross
subsystem boundaries. The intent is to preserve a recent timeline without
requiring heap allocation, filesystem access, a running scheduler, or graphics.

## Design constraints

- **Bounded:** 1,024 fixed-size records. The ring occupies a small, predictable
  amount of BSS and overwrites old events when full.
- **Allocation-free:** emit and snapshot do not call the heap, PMM, or VFS.
- **Interrupt-friendly:** writers do not acquire a spinlock or wait for another
  writer. This avoids self-deadlock when an interrupt records an event.
- **SMP-aware:** sequence allocation and payload fields use compiler atomic
  builtins. Each slot publishes a sequence number after writing its payload;
  snapshots verify that sequence before and after reading.
- **Best-effort:** a concurrent overwrite or an uncommitted record can be
  omitted from a snapshot. A writer never spins for a busy slot; if two CPUs
  collide on the same wrapped slot, one event can be absent from the ring while
  the event counter still records the attempted event. This is diagnostic
  history, not a durable journal or a synchronization primitive.
- **Counters:** per-event totals survive ring wraparound for the current boot.
  The overwritten estimate is max(total_events - KTRACE_CAPACITY, 0); a
  concurrent snapshot may be slightly stale.

## Current instrumentation

| Event | Payload |
|---|---|
| KTRACE_EVENT_BOOT_PHASE | phase identifier and result |
| KTRACE_EVENT_PMM_READY | total and free physical frames |
| KTRACE_EVENT_PMM_ALLOC_FAILURE | total and free frames at failure |
| KTRACE_EVENT_SCHED_SWITCH | outgoing and incoming PID; CPU ID |
| KTRACE_EVENT_PCI_SCAN_COMPLETE | discovered device count and MCFG region count |
| KTRACE_EVENT_PANIC | exception number and fault address when available |
| KTRACE_EVENT_DRIVER_INIT | driver initialization milestone |

Events are intentionally numeric and fixed-width. This keeps writes cheap and
makes the record format independent of serial formatting, heap state, and
user-facing text. New subsystems should add event IDs and stable argument
meanings rather than embedding arbitrary strings in interrupt paths.

## API

- ktrace_emit(type, cpu_id, arg0, arg1) appends one record.
- ktrace_snapshot(output, capacity) copies the most recent records that remain
  stable during the read. The returned records are in sequence order, but a
  concurrent writer can cause records to be skipped.
- ktrace_event_count(type) returns the boot-lifetime count for a type.
- ktrace_dump_serial(max_records) prints up to 32 recent records through the
  existing serial console. Fatal panic paths include a bounded trace dump.

The trace ring is initialized by static zero-initialized storage and is safe to
use during early initialization. It must not be treated as a replacement for
panic-safe raw register capture, a hardware event log, or external CI/QEMU
testing. Event arguments must not contain secrets or user-provided strings.
