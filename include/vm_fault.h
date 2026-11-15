/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VM_FAULT_H
#define VM_FAULT_H

#include <stddef.h>
#include <stdint.h>
#include "paging.h"
#include "vm_region.h"

typedef enum
{
    VM_FAULT_RESOLVED = 0,  /* page mapped; restart the faulting instruction */
    VM_FAULT_FATAL,         /* process must be terminated */
    VM_FAULT_OUT_OF_MEMORY  /* valid access, but no frame could be provided */
} vm_fault_result_t;

typedef struct vm_fault_stats
{
    uint64_t resolved;
    uint64_t already_mapped;
    uint64_t fatal;
    uint64_t out_of_memory;
    uint64_t prefaulted;
    uint64_t by_class[VM_FAULT_NOT_DEMAND + 1];
} vm_fault_stats_t;

/*
 * Resolve a user-mode page fault. cr2 and error_code must be captured by the
 * exception entry before any other fault can occur. On VM_FAULT_FATAL or
 * VM_FAULT_OUT_OF_MEMORY nothing has been mapped and the address space is
 * unchanged; *class_out (optional) reports the classification.
 */
vm_fault_result_t vm_handle_user_fault(paging_address_space_t* space,
                                       uintptr_t cr2, uint64_t error_code,
                                       vm_fault_class_t* class_out);

/*
 * Make every page of [address, address + length) resident when its region
 * permits the access, so kernel code can validate and then touch user buffers
 * that have not been faulted in yet. Returns 1 on success; never maps pages
 * outside a demand-backed region.
 */
int vm_user_range_ensure(paging_address_space_t* space, uintptr_t address,
                         size_t length, int writable);
/* Ensure, then validate with paging_user_range_valid(). */
int vm_user_range_valid(paging_address_space_t* space, uintptr_t address,
                        size_t length, int writable);

/* Snapshot of counters (relaxed; for diagnostics and tests). */
void vm_fault_get_stats(vm_fault_stats_t* out);

/*
 * Test-only fault injection: make the next `count` demand-frame allocations
 * for pages inside [window_start, window_start + window_length) fail as if
 * physical memory were exhausted. The window keeps unrelated processes from
 * consuming an armed failure.
 */
void vm_fault_inject_alloc_failures(uintptr_t window_start, size_t window_length,
                                    uint32_t count);
uint32_t vm_fault_injected_failures_pending(void);

#endif
