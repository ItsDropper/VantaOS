# User virtual memory and demand paging

This document describes the per-process virtual-memory regions and the demand-paging fault handler. It describes the implementation as it exists; limits and unverified areas are listed explicitly at the end. Evidence terms follow [the documentation style guide](../STYLE.md).

## Scope

Implemented: per-address-space region tables, lazy zero-fill frame allocation, permission enforcement, recoverable-versus-fatal fault classification, instruction restart, out-of-memory containment, user-fault termination and reclamation, and page-table/frame teardown.

Also implemented (host-tested only; see [Run-time mapping](#run-time-mapping-mmap-munmap-brk)): anonymous `mmap`, `munmap` (including partial and middle-punch unmaps) and `brk`.

Not implemented, by design: swapping, copy-on-write, dynamic linking, memory-mapped files, file-backed `mmap`, and file-backed lazy loading (see [Limits](#limits)).

## Address layout

User mappings live in PML4 slot 1 (`PAGING_USER_BASE` = `0x8000000000` through `PAGING_USER_END`, 512 GiB). Everything outside that window — the kernel identity map, heap, MMIO and firmware windows, and the whole canonical upper half — is never resolved by the fault handler.

```
0x8000000000  PAGING_USER_BASE
   ...        ELF segments / flat image / anonymous regions
   ...        guard page            (VM_REGION_GUARD, never mapped)
   ...        stack reserve, 64 pages (VM_REGION_STACK, demand-zero)
   ...        top 4 stack pages     (mapped eagerly: argc/argv/auxv)
PAGING_USER_END + 1                  stack top
```

## Components

| File | Responsibility |
|---|---|
| `include/vm_region.h`, `mm/vm_region.c` | Region table, validation, overlap/budget checks, and the pure fault classifier. No hardware access; host-tested. |
| `include/vm_fault.h`, `mm/vm_fault.c` | Fault resolution: frame allocation, zeroing, installation, syscall prefault, statistics, test-only allocation-failure injection. |
| `mm/user_mapping.c` | `paging_map_user_page_demand()` leaf installation and reclaim of demand frames during teardown. |
| `mm/paging.c` | Address-space lifecycle, short lifetime pins, and per-address-space page-table locks; `vm_space_t` is embedded in `paging_address_space_t`. |
| `kernel/panic.c`, `arch/x86_64/isr.asm` | Exception entry: CR2 capture, dispatch, restart-or-terminate. |
| `kernel/proc/process_elf.c`, `process_user.c` | Region setup for ELF and flat images; exit accounting. |
| `kernel/proc/process_exit_log.c` | Bounded log of reclaimed user processes (status and memory accounting). |

The region table lives inside the address space, so its lifetime is exactly the address space's lifetime, and the exception path reaches it from the CPU's current address space without taking the process-table lock.

## Regions

A region records a page-aligned half-open range, a type (`ELF`, `FLAT_IMAGE`, `ANON`, `STACK`, `GUARD`), permissions (`R`/`W`/`X`), a backing kind, and a count of resident demand pages.

| Backing | Meaning |
|---|---|
| `POPULATED` | Mapped by the creator before the process runs; frames are owned by the process (`user_pages`). A non-present access is never papered over. |
| `ZERO` | Frames are allocated and zeroed on first touch. |
| `NONE` | Guard; every access is fatal. |

`vm_region_validate()` rejects unaligned or empty ranges, address overflow, any range outside the user window (including the kernel half), writable-and-executable permissions, executable demand-zero memory, and inconsistent type/backing/permission combinations. `vm_space_add_region()` additionally rejects overlap, a full table (`VM_REGION_MAX`), and exceeding the 256 MiB per-space reservation budget (guards are free).

### How images are laid out

- **ELF:** Pages containing file bytes are mapped eagerly — the image buffer is not retained after loading. Pure `.bss` pages beyond the file data become a `ZERO` region. Executable segments are always fully populated.
- **Stack:** 64 pages are reserved. The top four are mapped eagerly to hold `argc`/`argv`; the remaining 60 fault in as the stack grows. One `GUARD` page sits directly below, so overflow is a clean, classified fault.
- **Flat images** (kernel-built test programs): one code page, one stack page, plus optional extra anonymous/guard regions via `process_create_user_with_regions()`.

## Page-fault flow

1. The exception stub saves state and calls `exception_handler()`. For vector 14 the handler reads **CR2 first**, before anything that could fault again.
2. Only faults from ring 3 reach the demand-paging path. A fault from ring 0 still takes the existing kernel-panic path; demand paging never runs for it.
3. `vm_space_classify_fault()` decides, without allocating:

   | Class | Condition | Outcome |
   |---|---|---|
   | `DEMAND_MAP` | Non-present, user-mode access inside a `ZERO` region that permits it | allocate and map |
   | `NO_REGION` | Address not covered by any region (including kernel addresses) | terminate |
   | `GUARD` | Inside a guard region | terminate |
   | `ACCESS_DENIED` | Non-present, but the region forbids that access (write to read-only, fetch from non-executable) | terminate |
   | `PROTECTION` | Page present but the access still faulted | terminate (no copy-on-write exists) |
   | `NOT_DEMAND` | Non-present inside an eagerly populated region | terminate |
   | `RESERVED_BIT` / `NOT_USER_MODE` | Corrupted tables / supervisor-origin error code | terminate |

4. For `DEMAND_MAP` the handler allocates a frame below the identity-map ceiling, zeroes it **before** publishing it, and installs a `PRESENT|USER|NX|SOFT_DEMAND` leaf (plus `WRITE` when the region is writable). If another path already populated the page (`EXISTS`), the spare frame is freed and the fault counts as resolved.
5. On success `exception_handler()` returns 1 and the stub resumes directly with `iretq`; RIP is unchanged, so the faulting instruction restarts. Every other outcome follows the existing path.
6. On failure the process is terminated with status `-1` (or `-12` for physical-memory exhaustion). Nothing was mapped, so the address space is unchanged and is reclaimed normally. The kernel logs `VANTAOS_USER_PROCESS_FAULT_PID=… CR2=… ERR=… CLASS=…` plus the legacy `…_UNMAPPED`, `…_WRITE_PROTECT`, `…_NX` markers and new `…_STACK_GUARD`, `…_KERNEL_ADDRESS`, `…_OOM` markers.

A fault from ring 3 holds no kernel locks, so termination takes the process-table lock with a blocking acquire; a try-lock could fail indefinitely under contention and turn a contained fault into a panic.

## Memory ownership and reclamation

- Demand frames are owned by the page tables. Their leaves carry the software bit `PAGE_SOFT_DEMAND` (PTE bit 9, ignored by hardware).
- Eagerly mapped frames remain owned by the process's `user_pages` list; reclaim never touches them (it frees only `SOFT_DEMAND` leaves), so there is no double free.
- `paging_destroy_address_space()` still refuses a space that is active on a CPU or has pending shootdowns. When it proceeds it detaches the user table tree under the paging lock and frees page tables and demand frames **after releasing that lock**, so a large address space does not stall unrelated mapping work.
- Syscalls that read or write user buffers call `vm_user_range_valid()`, which first makes buffer pages resident when (and only when) they lie in a demand-backed region that permits the access, then runs the existing validation. A buffer in a guard or unreserved range still fails with `-EFAULT`. Pages prefaulted this way are ordinary demand pages and are reclaimed the same way.

## SMP and TLB reasoning

- Not-present to present transitions need no TLB shootdown: x86 does not cache not-present translations, and a process executes on one CPU at a time. The demand-map path therefore does not use the cross-CPU shootdown, which keeps concurrent faults in unrelated processes from serializing on it. Removing or downgrading a mapping still goes through the existing `paging_tlb_shootdown()`.
- Locks: region lookup takes only the per-address-space `vm_space_t` lock, released before any allocation; zeroing happens with no lock held. User page-table walks and mutations take the address space's `page_table_lock`; a brief global lifecycle-lock pin increments `active_operations` before waiting for that lock and drops it after unlocking. The pin prevents address-space teardown while an operation is queued or active, but the global lock is not held during page-table walks, allocation, or shootdowns. Kernel/global mappings and address-space activation/destruction still use the global lifecycle lock. `munmap`/`brk` range release keeps its address-space lifetime pin across each shootdown and frame reclaim, but drops the PTE lock before waiting for remote acknowledgements and reacquires it afterward. This avoids waiting for an IPI with local interrupts disabled; unrelated address spaces can progress.
- Concurrent faults in different processes allocate distinct frames from the PMM (which has its own lock). Two CPUs cannot fault in the same address space at once because a process has a single execution context; the `EXISTS` path covers prefault/fault overlap on one CPU.
- Migration: a process that moves to another CPU finds no stale translation for pages mapped by a fault, since none was cached.
- Destruction versus faults: destruction requires the space not to be active on any CPU, so a fault (which runs on the process's own CPU) cannot overlap it.

## Diagnostics

- `vm_fault_get_stats()` — resolved, already-mapped, fatal, out-of-memory, prefault counters, and a count per classification.
- Per address space: demand faults, resident pages, fault events, and the last fault's class, address and error code.
- `process_exit_log_*()` — bounded log of reclaimed user processes recording exit status, demand faults, frames reclaimed, and the last fault. Zombies are only retained for user parents, so this is how a kernel observer sees how a child ended.

## Verification

| Layer | What | Evidence level |
|---|---|---|
| `scripts/test_vm_region.c` | Validation (alignment, overflow, boundaries, W^X), overlap, capacity, budget, classification matrix, accounting, and concurrent insertion from 8 threads; run plain, under ASan/UBSan, and under TSan | host-tested |
| `scripts/test_demand_paging_structure.py` | Source invariants: CR2 captured first, demand path only for CPL3, restart only on resolution, NX and `SOFT_DEMAND` on demand leaves, zero-before-map, reclaim frees only demand frames, teardown ordering, per-address-space lock/pin ordering, generated test programs current | host-tested (structural) |
| `os/selftest/demand_paging_selftest.c` + `kernel/selftest/vm/*.asm` | Real ring-3 programs asserting per-process exit status, demand-fault count, last fault class/address/error, and frames reclaimed; the runtime-mapping case additionally checks live mmap/munmap/brk syscall behavior and that explicit unmaps leave no resident demand frames | emulated (QEMU; CI run pending) |

The QEMU cases are: lazy zero-fill with read-first and write-first touches and pattern integrity; fetch from demand-zero memory; write to a read-only page (first touch and after a read fault-in); guard region; unreserved address; kernel-address read/write; canonical-high and non-canonical addresses; syscall prefault; allocation failure (injected, window-scoped); stack growth; stack overflow into the guard; ELF `.bss`; 20 repeated allocate/release cycles (nonzero data left behind must not reappear, and the PMM must not drift); concurrent faulting by 4–8 processes across CPUs with distinct patterns to detect shared frames; and runtime mapping through real ring-3 syscalls (four-page mmap, middle-punch plus head/tail munmap, fresh zero-filled remapping, brk growth/shrink/regrowth, and zero resident pages after explicit release).

Run at 1, 2 and 4 vCPUs under GRUB and under UEFI/OVMF. See [Testing](../build/TESTING.md).

## Run-time mapping (mmap, munmap, brk)

Status: **host-tested; ring-3 QEMU regression added; CI execution pending; not hardware-verified.** The `RUNTIME_MAPPING` program runs as a loaded ELF process (its flat-image test stack would correctly collide with a `brk` growing immediately after the image) and issues these syscalls from ring 3 against the real page tables and TLB shootdown path.

- `mmap(length, prot, 0)` reserves a top-down gap in `[VM_MMAP_WINDOW_LOW, lowest stack/guard)` (64 GiB above the ELF base) as an anonymous demand-zero region: readable, optionally writable, never executable. Length is rounded up to pages. The result is the address, always positive.
- `munmap` is a three-phase operation: `vm_space_unmap_check` (pure; refuses the whole request if any part is not anonymous or a middle punch has no free region slot), then per affected region `vm_space_unmap_next`, `paging_release_user_demand_range` and `vm_space_unmap_commit`. The release walk clears each PTE, shoots the translation down on every CPU and only then returns the frame to the PMM. It refuses (before changing anything) if a present leaf in the range is not `PAGE_SOFT_DEMAND`, so frames the page tables do not own are never freed.
- `brk` is lazily anchored to the end of the highest ELF/flat-image region, capped at 128 MiB and the mmap window, and backed by one region flagged `VM_REGION_FLAG_HEAP`. A request that cannot be honoured returns the unchanged break. Shrinking uses the same release walk.
- A failure part-way through a multi-region `munmap` returns `-EIO` with the table coherent: earlier pieces stay unmapped, and the serial log carries `VANTAOS_MUNMAP_INCONSISTENT`.

Evidence: `scripts/test_vm_region.c` (region logic, a randomized model test against a per-page shadow, 13 deliberate-mutation checks run by hand), `scripts/test_syscall_vm.c` (syscall glue with a stubbed page-table layer, including injected release failures), and the new `RUNTIME_MAPPING` ring-3 QEMU case. The latter is specifically intended to exercise the real page-table walk, TLB shootdown and PMM; its CI result must be green before this is considered verified.

## Limits

- **File-backed lazy loading is not implemented.** ELF file pages are populated eagerly because the source image is not retained. Only `.bss`, the stack, and anonymous regions are lazy.
- Run-time mapping is anonymous-only: no file-backed `mmap`, no address hints or `MAP_FIXED`, no `mprotect`/`mremap`, and `munmap` refuses ranges that are not wholly inside anonymous mappings (so holes in a range, the image, the stack and the `brk` heap are `EINVAL` rather than silently skipped). Not QEMU- or hardware-verified; see below.
- Per-process region count (40), reservation (256 MiB) and a 64-page eager-page budget bound resource use. A syscall over a very large lazy buffer populates it page by page.
- Resident demand pages are not reclaimed under memory pressure (no swapping); exhaustion terminates the faulting process.
- Prefaulting a very large lazy buffer in one syscall, and reclaiming a very large address space, run with interrupts disabled on that CPU; neither is capped or batched yet. There is also no global demand-frame cap or kernel reserve, so several processes can exhaust the below-1-GiB pool and cause OOM terminations of unrelated processes.
- `paging_unmap_user_page()` must not be used on demand leaves (their frames are owned by the page tables); `munmap`/`brk` use `paging_release_user_demand_range()`, which frees the frame itself.
- The paging lock is still global for table updates; per-address-space table locking is future work.
- Page-fault handling on real hardware has not been exercised. QEMU's TCG reports a non-canonical access as `#PF` where hardware raises `#GP`; both are contained.
- Kernel-mode accesses to unmapped user memory remain fatal by design; the kernel only touches user memory after `vm_user_range_valid()`.
