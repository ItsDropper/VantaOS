/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef VM_REGION_H
#define VM_REGION_H

#include <stddef.h>
#include <stdint.h>
#include <spinlock.h>

/*
 * Per-address-space virtual memory regions.
 *
 * A region reserves a page-aligned user virtual range with fixed permissions.
 * Physical frames for VM_BACKING_ZERO regions are allocated lazily by the
 * page-fault handler (mm/vm_fault.c); VM_BACKING_POPULATED regions are mapped
 * eagerly by their creator, which also owns those frames. VM_BACKING_NONE
 * regions (guards) are never mapped and every access to them is fatal.
 *
 * The region table lives inside paging_address_space_t, so its lifetime is
 * exactly the address space's lifetime and the exception path can reach it
 * from CR3 ownership alone, without touching the process-table lock.
 */

#define VM_PROT_READ  0x1U
#define VM_PROT_WRITE 0x2U
#define VM_PROT_EXEC  0x4U
#define VM_PROT_MASK  0x7U

#define VM_REGION_MAX 40U
/* Upper bound on reserved (not necessarily resident) bytes per space. */
#define VM_SPACE_MAX_RESERVED_BYTES (256ULL * 1024ULL * 1024ULL)

typedef enum
{
    VM_REGION_NONE = 0,
    VM_REGION_ELF,        /* ELF PT_LOAD segment, or its .bss tail */
    VM_REGION_FLAT_IMAGE, /* flat code/data image created by the kernel */
    VM_REGION_ANON,       /* anonymous zero-fill memory */
    VM_REGION_STACK,      /* process stack (grows by demand inside its range) */
    VM_REGION_GUARD       /* inaccessible; any touch terminates the process */
} vm_region_type_t;

typedef enum
{
    VM_BACKING_NONE = 0,
    VM_BACKING_POPULATED, /* mapped by the creator; frames owned by the process */
    VM_BACKING_ZERO       /* demand-allocated, zero-filled frames */
} vm_backing_t;

typedef struct vm_region
{
    uintptr_t start;      /* inclusive, page aligned */
    uintptr_t end;        /* exclusive, page aligned */
    uint8_t type;         /* vm_region_type_t */
    uint8_t prot;         /* VM_PROT_* */
    uint8_t backing;      /* vm_backing_t */
    uint8_t reserved;
    uint32_t resident_pages; /* demand-faulted pages currently mapped */
} vm_region_t;

typedef struct vm_space
{
    spinlock_t lock;
    uint32_t region_count;
    uint64_t reserved_bytes;
    /*
     * Accounting for diagnostics and leak checks. Written only by the CPU
     * running the owning process (and read after it died), so plain atomics
     * suffice; none of these gate any decision.
     */
    uint32_t demand_faults;      /* pages populated by the fault handler */
    uint32_t resident_pages;     /* demand pages currently mapped */
    uint32_t fault_events;       /* user faults seen, resolved or not */
    uint32_t last_fault_class;   /* vm_fault_class_t of the latest fault */
    uint64_t last_fault_address;
    uint64_t last_fault_error;
    vm_region_t regions[VM_REGION_MAX];
} vm_space_t;

/* Page-fault error-code bits (Intel SDM Vol. 3A, 4.7). */
#define VM_PF_PRESENT  0x01U
#define VM_PF_WRITE    0x02U
#define VM_PF_USER     0x04U
#define VM_PF_RESERVED 0x08U
#define VM_PF_INSTR    0x10U

typedef enum
{
    VM_FAULT_DEMAND_MAP = 0,   /* valid, non-present: allocate and map */
    VM_FAULT_ALREADY_MAPPED,   /* internal: raced with another populator */
    VM_FAULT_NO_REGION,        /* address is not reserved by any region */
    VM_FAULT_GUARD,            /* inside a guard region */
    VM_FAULT_PROTECTION,       /* present page, access violates the PTE */
    VM_FAULT_ACCESS_DENIED,    /* non-present, region forbids this access */
    VM_FAULT_RESERVED_BIT,     /* corrupted page table (RSVD error bit) */
    VM_FAULT_NOT_USER_MODE,    /* error code lacks the user-mode bit */
    VM_FAULT_NOT_DEMAND        /* region is not demand-backed (populated) */
} vm_fault_class_t;

void vm_space_init(vm_space_t* space);
/* Remove every region; used when the owning address space is destroyed. */
void vm_space_clear(vm_space_t* space);

/*
 * Pure validation of a candidate region: alignment, overflow, user-range
 * containment, W^X, and type/backing/permission consistency. Returns 1 when
 * acceptable. Performs no allocation and takes no locks.
 */
int vm_region_validate(uintptr_t start, size_t length, uint32_t prot,
                       vm_region_type_t type, vm_backing_t backing);

/* Validate, check for overlap and the reservation budget, then insert. */
int vm_space_add_region(vm_space_t* space, uintptr_t start, size_t length,
                        uint32_t prot, vm_region_type_t type,
                        vm_backing_t backing);

/* Copy out the region containing address. Returns 1 if one exists. */
int vm_space_find_region(vm_space_t* space, uintptr_t address,
                         vm_region_t* out);

/*
 * Classify a user page fault against the region table. Pure with respect to
 * hardware state; it never allocates and never maps. When the result is
 * VM_FAULT_DEMAND_MAP, *region_out holds the covering region.
 */
vm_fault_class_t vm_space_classify_fault(vm_space_t* space, uintptr_t address,
                                         uint64_t error_code,
                                         vm_region_t* region_out);

/* Account one demand page mapped into / reclaimed from address's region. */
void vm_space_note_resident(vm_space_t* space, uintptr_t address,
                            int32_t delta);

const char* vm_fault_class_name(vm_fault_class_t cls);

#endif
