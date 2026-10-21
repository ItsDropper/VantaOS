# Reliability audit: memory, SMP, faults, and storage

Date: 2026-10-21

This audit records what the current tests establish and what remains unproven. It is not a claim of complete kernel verification.

## Memory management

Current automated coverage includes:

- Boot-info normalization and physical-memory reserved-range precedence.
- Physical allocator alignment, distinct allocations, accounting, invalid unaligned frees, and duplicate frees.
- Heap alignment, allocation data integrity, invalid unaligned frees, duplicate frees, and accounting restoration.
- User page mapping with supervisor-only kernel memory access rejection.
- User buffer validation at syscall boundaries.
- Address-space teardown only after switching away from the target CR3, with active-CPU and pending-shootdown checks.
- TLB shootdown failure enters the fatal kernel halt path.

This change tightens the heap self-test to require the free-payload accounting to return exactly to its baseline after the probe. Heap total size includes block headers while used/free counters count payload bytes, so they must not be asserted to sum to total size.

Remaining gaps:

- No exhaustive allocation-failure injection at each page-table allocation level.
- No randomized page-map/unmap stress with concurrent address-space switches.
- No general demand paging or copy-on-write.
- No proof of allocator correctness under arbitrary corruption or all possible SMP interleavings.

## SMP and panic behavior

Existing QEMU regression coverage includes 1-, 2-, and 4-vCPU boots, AP startup/topology markers, scheduler stress, cross-CPU wakeup/reschedule IPI behavior, process migration, and ticket-lock contention. The panic path serializes first-entry ownership, emits a serial report before stopping peers, and uses an NMI broadcast when APIC is enabled. Panic-broadcast NMIs are now handled before CPL3 fault recovery, and CPL3 process-fault handling uses non-blocking process-table operations; lock contention fails closed to the panic path.

The new controlled panic workflow separately tests the real panic renderer after graphics initialization, validates the serial report, captures a screenshot, and checks that the captured frame is red-dominant.

Remaining gaps:

- The controlled panic is invoked synchronously from the boot path; it does not prove every exception vector or every panic entry race.
- No injected double-fault/triple-fault test.
- No test where a peer CPU is deliberately stalled while holding each possible subsystem lock.
- No physical SMP hardware verification.

## Ring-3 exception containment

The existing user-mode self-test enters CPL3, verifies the test syscall, attempts a supervisor-only kernel-memory read, checks user GS-base isolation, and recovers to a known kernel continuation. The process fault path separately marks user-originated exceptions and terminates the current user process with a non-blocking process-table operation when its process/address-space ownership checks pass.

Remaining gaps:

- No fuzzing of malformed ELF images or all syscall argument combinations.
- No exhaustive test of faults during every privilege-transition instruction.
- The self-test's controlled recovery path is not a substitute for adversarial process isolation testing.

## FAT32 and partition handling

Existing QEMU tests exercise ATA-backed filesystem mutation/readback, multi-cluster I/O, long-name create/rename/delete, persistent readback across reboot, partition scanning, and rejection of ambiguous valid FAT32 volumes. Partition scanner host tests cover GPT primary/backup metadata, CRCs, bounds, and malformed table data.

The new host LFN suite independently checks single- and multi-entry sequence decoding, orphan and duplicate entries, invalid ordinals, checksum discontinuities, and malformed UTF-16 surrogate sequences.

Remaining gaps:

- No exhaustive corrupted FAT-chain corpus or randomized directory-entry fuzzing.
- No crash-atomic replacement or journaling.
- No concurrent FAT32 mutation locking/transaction guarantee.
- Unicode LFN decoding exists, but Unicode filename creation is not supported.
- Removable-device hot-unplug cleanup and multi-volume mounting are not established by these tests.

## CI interpretation

A passing test means its explicit assertions passed in the stated environment. QEMU and host tests do not establish physical-hardware compatibility. The dedicated workflows are:

- `panic-screen.yml` — controlled red panic-screen QEMU regression and artifact capture.
- `storage-memory-regressions.yml` — host tests for FAT32 LFN decoding, partition scanning, and panic formatting.
- `build.yml` — the broader kernel, SMP, user-mode, storage, and QEMU/OVMF regression suite.
