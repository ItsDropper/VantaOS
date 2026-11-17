/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>
#include "vm_region.h"

#define PAGE_SIZE 4096U
#define PAGE_PRESENT 0x001U
#define PAGE_WRITE   0x002U
#define PAGE_USER    0x004U
#define PAGE_NX      (1ULL << 63)

/* Persistent uncached scanout and transient device-MMIO virtual windows. */
#define PAGING_GRAPHICS_BASE 0xD0000000ULL
#define PAGING_GRAPHICS_SCANOUT_SIZE (16ULL * 1024ULL * 1024ULL)
#define PAGING_GRAPHICS_SIZE (2ULL * PAGING_GRAPHICS_SCANOUT_SIZE)

/* Shared virtual window for uncached device MMIO mappings. */
#define PAGING_MMIO_BASE 0xE0000000ULL
#define PAGING_MMIO_SIZE (6ULL * 1024ULL * 1024ULL)

/* User mappings live in PML4 slot 1, away from the shared kernel mappings. */
#define PAGING_USER_BASE 0x0000008000000000ULL
#define PAGING_USER_END  0x000000FFFFFFFFFFULL

#define PAGING_KERNEL_DIRECTORY_INDEX 256U
#define PAGING_GRAPHICS_DIRECTORY_INDEX 832U

typedef struct paging_address_space
{
    uint64_t* directory;
    uintptr_t directory_physical;
    int used;
    /* Protected by the global lifecycle lock; bit N means CPU N runs here. */
    volatile uint32_t active_cpu_mask;
    /* A short global-lock pin prevents teardown while an operation waits/runs. */
    volatile uint32_t active_operations;
    /* Independent address spaces never contend on this page-table lock. */
    spinlock_t page_table_lock;
    /* Outstanding map/unmap shootdowns prevent page-table destruction. */
    volatile uint32_t pending_shootdowns;
    /* Region table consulted by the demand-paging fault handler. */
    vm_space_t vm;
} paging_address_space_t;

/* Software PTE ownership bits (ignored by hardware). Private demand leaves
 * own their frames directly; shared leaves own one reference in vm_page_cache. */
#define PAGE_SOFT_DEMAND (1ULL << 9)
#define PAGE_SOFT_SHARED (1ULL << 10)

typedef enum
{
    PAGING_DEMAND_FAILED = 0, /* invalid request or no memory for tables */
    PAGING_DEMAND_MAPPED,     /* leaf installed by this call */
    PAGING_DEMAND_EXISTS      /* leaf already present; nothing changed */
} paging_demand_result_t;

void paging_initialize(void);
/* CPU-reported physical address limit, initialized with paging. */
uint64_t paging_physical_address_limit(void);
/*
 * Make a physical firmware-table range between 1 GiB and 3 GiB readable at the
 * same virtual address (uncached). Returns 1 when the whole range is reachable.
 */
int paging_map_firmware_range(uint64_t physical_address, uint64_t length);
int paging_map_mmio_page(uintptr_t virtual_address, uintptr_t physical_address);
int paging_map_page(uintptr_t virtual_address, uintptr_t physical_address);
int paging_map_user_page(paging_address_space_t* address_space,
                         uintptr_t virtual_address,
                         uintptr_t physical_address,
                         int writable);
int paging_map_user_page_ex(paging_address_space_t* address_space,
                            uintptr_t virtual_address,
                            uintptr_t physical_address,
                            int writable, int executable);
/*
 * Install a demand-allocated, non-executable user leaf. No TLB shootdown is
 * needed: a not-present translation is never cached, and a process runs on at
 * most one CPU at a time. Ownership of the frame passes to the page tables
 * (PAGE_SOFT_DEMAND) only when PAGING_DEMAND_MAPPED is returned.
 */
paging_demand_result_t paging_map_user_page_demand(
    paging_address_space_t* address_space, uintptr_t virtual_address,
    uintptr_t physical_address, int writable);
paging_demand_result_t paging_map_user_page_shared(
    paging_address_space_t* address_space, uintptr_t virtual_address,
    uintptr_t physical_address);
/* Lifetime counters for diagnostics and regression tests. */
uint64_t paging_demand_frames_reclaimed(void);
/* Frames freed by paging_release_user_demand_range (munmap/brk shrink). */
uint64_t paging_demand_frames_released(void);
/*
 * Free the demand-allocated frames of [start, end) in a user address space and
 * count the demand pages still resident in the neighbouring pieces
 * [head_start, start) and [end, tail_end) (pass head_start == start or
 * tail_end == end to skip a side). Every address must be page aligned and
 * head_start <= start < end <= tail_end.
 *
 * Fails (returns 0, nothing changed) if any present leaf in [start, end) is
 * not PAGE_SOFT_DEMAND: frames the page tables do not own are never freed
 * here. For each demand leaf the PTE is cleared, the translation is shot down
 * on every CPU, and only then is the frame returned to the PMM.
 *
 * The caller must be the only mutator of this address space's mappings (true
 * for a process changing its own space from a syscall).
 */
int paging_release_user_demand_range(paging_address_space_t* address_space,
                                     uintptr_t head_start, uintptr_t start,
                                     uintptr_t end, uintptr_t tail_end,
                                     uint32_t* head_resident,
                                     uint32_t* released,
                                     uint32_t* tail_resident);
int paging_nx_available(void);
int paging_unmap_user_page(paging_address_space_t* address_space,
                           uintptr_t virtual_address);
/* Validate a complete user buffer before the kernel dereferences it. */
int paging_user_range_valid(paging_address_space_t* address_space,
                            uintptr_t virtual_address, size_t length,
                            int writable);
uint64_t paging_get_page_flags(uintptr_t virtual_address);
void paging_unmap_page(uintptr_t virtual_address);
uintptr_t paging_get_physical(uintptr_t virtual_address);
paging_address_space_t* paging_create_address_space(void);
int paging_destroy_address_space(paging_address_space_t* address_space);
/* Same, also reporting how many demand-allocated frames were reclaimed. */
int paging_destroy_address_space_counted(
    paging_address_space_t* address_space, uint32_t* demand_frames_out);
void paging_switch_address_space(paging_address_space_t* address_space);
/* Register a newly initialized CPU in its initial address space. */
int paging_register_cpu(uint32_t logical_id,
                        paging_address_space_t* address_space);
/* Synchronously invalidate one virtual page on every online CPU using space. */
int paging_tlb_shootdown(paging_address_space_t* address_space,
                         uintptr_t virtual_address);
/* Called only by the dedicated TLB-shootdown IPI handler. */
void paging_tlb_shootdown_handle_interrupt(void);
paging_address_space_t* paging_get_kernel_address_space(void);
paging_address_space_t* paging_get_current_address_space(void);

#endif
