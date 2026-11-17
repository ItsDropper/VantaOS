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
 * VM_BACKING_ZERO regions allocate zero-filled frames lazily; VM_BACKING_FILE
 * regions copy immutable retained ELF bytes into private frames on demand;
 * VM_BACKING_POPULATED regions are mapped eagerly by their creator, which
 * owns those frames. VM_BACKING_NONE regions (guards) are never mapped.
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

/* Region flags. */
#define VM_REGION_FLAG_HEAP 0x1U /* the brk heap; munmap may not touch it */

/*
 * Anonymous mmap allocations are placed top-down inside
 * [VM_MMAP_WINDOW_LOW, lowest stack/guard region), 64 GiB above the ELF load
 * base, which keeps them clear of statically linked images and the brk heap.
 */
#define VM_MMAP_WINDOW_LOW ((uintptr_t)PAGING_USER_BASE + 0x1000000000ULL)

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
    VM_BACKING_ZERO,      /* demand-allocated, zero-filled frames */
    VM_BACKING_FILE       /* private demand pages copied from retained ELF bytes */
} vm_backing_t;

typedef struct vm_region
{
    uintptr_t start;      /* inclusive, page aligned */
    uintptr_t end;        /* exclusive, page aligned */
    uint8_t type;         /* vm_region_type_t */
    uint8_t prot;         /* VM_PROT_* */
    uint8_t backing;      /* vm_backing_t */
    uint8_t flags;        /* VM_REGION_FLAG_* */
    uint32_t resident_pages; /* demand-faulted pages currently mapped */
    const uint8_t* file_data; /* immutable kernel-owned image backing, if FILE */
    uint64_t file_offset;     /* first source byte in the ELF segment */
    uint64_t file_bytes;      /* bytes belonging to the ELF segment */
    uint32_t file_prefix;     /* zero bytes before segment start in first page */
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
    /* brk heap. brk_base == 0 means "not yet initialized". */
    uintptr_t brk_base;          /* page aligned; first byte of the heap */
    uintptr_t brk_cur;           /* current program break, byte granular */
    const uint8_t* image_backing; /* retained immutable ELF image, address-space owned */
    uint64_t image_backing_size;
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
/* Attach one immutable image buffer whose lifetime is the address-space lifetime. */
int vm_space_set_image_backing(vm_space_t* space, const void* image,
                               size_t image_size);

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
/* Reserve ELF pages that are copied from the retained image on first touch. */
int vm_space_add_file_region(vm_space_t* space, uintptr_t start,
                             size_t length, uint32_t prot,
                             uint64_t file_offset, uint32_t file_prefix,
                             uint64_t file_bytes);

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

/* ---- Run-time mapping: mmap / munmap / brk ------------------------------ */

/*
 * Pick a free range for an anonymous mapping and reserve it, all under one
 * lock hold. Returns 1 and sets *address_out on success; 0 if the request is
 * invalid (length zero or unaligned after rounding, bad prot, W^X) or no gap,
 * region slot or reservation budget is available. Nothing changes on failure.
 */
int vm_space_map_anon(vm_space_t* space, size_t length, uint32_t prot,
                      uintptr_t* address_out);

/*
 * Unmapping is three phases because the page-table walk that frees demand
 * frames lives outside this file:
 *   1. vm_space_unmap_check   - pure; decides whether the whole range can be
 *                               unmapped, with no partial effects possible.
 *   2. vm_space_unmap_next    - describe the next affected region piece.
 *   3. (caller) release the piece's demand frames and count the resident pages
 *      left on each side, then vm_space_unmap_commit.
 * The caller must be the only mutator of the space (a process only runs on
 * one CPU at a time, and munmap is issued by that process itself).
 */
typedef enum
{
    VM_UNMAP_FULL = 1, /* whole region removed */
    VM_UNMAP_FRONT,    /* [original.start, end) removed; tail remains */
    VM_UNMAP_BACK,     /* [start, original.end) removed; head remains */
    VM_UNMAP_SPLIT     /* middle removed; head and tail remain */
} vm_unmap_kind_t;

typedef struct vm_unmap_step
{
    vm_region_t original; /* snapshot of the affected region */
    uintptr_t start;      /* removed portion, page aligned, inside original */
    uintptr_t end;
    uint8_t kind;         /* vm_unmap_kind_t */
} vm_unmap_step_t;

/*
 * The range must be page aligned, inside the user window, and fully covered
 * by VM_REGION_ANON regions that are not the heap (holes, ELF, stack, guard
 * and heap regions are rejected), and a middle-punch must have a free slot.
 */
int vm_space_unmap_check(vm_space_t* space, uintptr_t start, size_t length);
int vm_space_unmap_next(vm_space_t* space, uintptr_t start, uintptr_t end,
                        vm_unmap_step_t* step);
/*
 * left/right_resident: resident demand pages remaining in the head/tail
 * pieces after the caller released the removed portion (ignored when that
 * piece does not exist). Returns 0 if the region table no longer matches
 * step->original (nothing is changed).
 */
int vm_space_unmap_commit(vm_space_t* space, const vm_unmap_step_t* step,
                          uint32_t left_resident, uint32_t right_resident);

/*
 * Program break. brk is lazily initialized to the end of the highest ELF or
 * flat-image region. These calls only manage bookkeeping and regions; freeing
 * pages on shrink uses the unmap phases above.
 */
int vm_space_brk_get(vm_space_t* space, uintptr_t* base_out,
                     uintptr_t* current_out);
/* Grow to `requested` (> current). 1 on success; 0 changes nothing. */
int vm_space_brk_grow(vm_space_t* space, uintptr_t requested);
/*
 * Shrink: describe the page range to release for `requested` (< current).
 * Returns 1 and sets *release_start/end (equal when no whole page goes away).
 */
int vm_space_brk_shrink_plan(vm_space_t* space, uintptr_t requested,
                             uintptr_t* release_start, uintptr_t* release_end);
/* After the release: set the break and the heap region's resident count. */
int vm_space_brk_shrink_commit(vm_space_t* space, uintptr_t requested,
                               uint32_t heap_resident);

/* Heap cap: the break may not pass the mmap window or this many bytes. */
#define VM_BRK_MAX_BYTES (128ULL * 1024ULL * 1024ULL)

const char* vm_fault_class_name(vm_fault_class_t cls);

#endif
