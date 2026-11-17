/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
/*
 * HOST TEST of kernel/syscall_vm.c against the real mm/vm_region.c with a
 * stubbed page-table layer (paging_release_user_demand_range). It checks the
 * syscall logic, error mapping and frame accounting; it is not evidence that
 * the real page-table walk or TLB shootdown works (that needs QEMU/hardware).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "paging.h"
#include "syscall_vm.h"
#include "vm_region.h"

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    exit(1); } } while (0)

#define P 4096U
#define RW (VM_PROT_READ | VM_PROT_WRITE)
#define TOP ((uintptr_t)PAGING_USER_END + 1U)
#define E(n) (-(int64_t)(n))

/* ---- stub page tables: the set of pages with a present demand frame ---- */
#define MAXP 8192U
static uintptr_t present[MAXP];
static unsigned npresent;
static uint64_t freed_total;
static int fail_after;          /* >0: fail the Nth call to release; 0: never */
static int release_calls;

static int find_present(uintptr_t a)
{
    for (unsigned i = 0; i < npresent; i++) if (present[i] == a) return (int)i;
    return -1;
}
static void touch(paging_address_space_t* sp, uintptr_t a)
{
    if (find_present(a) < 0) {
        CHECK(npresent < MAXP);
        present[npresent++] = a;
        vm_space_note_resident(&sp->vm, a, 1);
    }
}
static int count_in(uintptr_t lo, uintptr_t hi)
{
    int n = 0;
    for (unsigned i = 0; i < npresent; i++) n += present[i] >= lo && present[i] < hi;
    return n;
}

int paging_release_user_demand_range(paging_address_space_t* sp,
                                     uintptr_t head_start, uintptr_t start,
                                     uintptr_t end, uintptr_t tail_end,
                                     uint32_t* head, uint32_t* released,
                                     uint32_t* tail)
{
    (void)sp;
    release_calls++;
    if (fail_after && release_calls == fail_after) return 0;
    CHECK(head_start <= start && start < end && end <= tail_end);
    CHECK(((head_start | start | end | tail_end) & (P - 1U)) == 0U);
    *head = (uint32_t)count_in(head_start, start);
    *tail = (uint32_t)count_in(end, tail_end);
    uint32_t n = 0;
    for (unsigned i = 0; i < npresent;) {
        if (present[i] >= start && present[i] < end) {
            present[i] = present[--npresent];
            n++;
        } else i++;
    }
    freed_total += n;
    *released = n;
    return 1;
}

static paging_address_space_t sp;
static void fresh(void)
{
    memset(&sp, 0, sizeof(sp));
    vm_space_init(&sp.vm);
    npresent = 0; freed_total = 0; fail_after = 0; release_calls = 0;
}
static void add_image(void)
{
    CHECK(vm_space_add_region(&sp.vm, PAGING_USER_BASE, 3U * P,
          VM_PROT_READ | VM_PROT_EXEC, VM_REGION_ELF, VM_BACKING_POPULATED));
    CHECK(vm_space_add_region(&sp.vm, PAGING_USER_BASE + 3U * P, 2U * P, RW,
          VM_REGION_ELF, VM_BACKING_ZERO));
}

/* Every resident page the stub knows about must be accounted in the table. */
static void consistent(void)
{
    CHECK(sp.vm.resident_pages == (uint32_t)npresent);
    uint32_t sum = 0;
    for (uint32_t i = 0; i < sp.vm.region_count; i++) {
        const vm_region_t* r = &sp.vm.regions[i];
        CHECK(r->resident_pages == (uint32_t)count_in(r->start, r->end));
        sum += r->resident_pages;
    }
    CHECK(sum == sp.vm.resident_pages);
    for (unsigned i = 0; i < npresent; i++) {
        vm_region_t r;
        CHECK(vm_space_find_region(&sp.vm, present[i], &r));   /* no orphan frame */
    }
}

static void test_mmap(void)
{
    fresh();
    CHECK(syscall_vm_mmap(NULL, P, 1, 0) == E(22));
    CHECK(syscall_vm_mmap(&sp, 0, 1, 0) == E(22));
    CHECK(syscall_vm_mmap(&sp, P, 0, 0) == E(22));        /* no access */
    CHECK(syscall_vm_mmap(&sp, P, 4, 0) == E(22));        /* exec refused */
    CHECK(syscall_vm_mmap(&sp, P, 3 | 4, 0) == E(22));
    CHECK(syscall_vm_mmap(&sp, P, 8, 0) == E(22));
    CHECK(syscall_vm_mmap(&sp, P, 1, 1) == E(22));        /* flags reserved */
    CHECK(syscall_vm_mmap(&sp, P, 1, 0x100000000ULL) == E(22));
    CHECK(syscall_vm_mmap(&sp, VM_SPACE_MAX_RESERVED_BYTES + 1U, 1, 0) == E(12));
    CHECK(syscall_vm_mmap(&sp, (uint64_t)-1, 1, 0) == E(12));
    CHECK(sp.vm.region_count == 0U);

    int64_t ro = syscall_vm_mmap(&sp, 100, 1, 0);
    CHECK(ro == (int64_t)(TOP - P));
    int64_t wo = syscall_vm_mmap(&sp, 2U * P, 2, 0);       /* write implies read */
    int64_t rw = syscall_vm_mmap(&sp, P, 3, 0);
    CHECK(wo == ro - 2 * (int64_t)P && rw == wo - (int64_t)P);
    vm_region_t r;
    CHECK(vm_space_find_region(&sp.vm, (uintptr_t)ro, &r) && r.prot == VM_PROT_READ);
    CHECK(vm_space_find_region(&sp.vm, (uintptr_t)wo, &r) && r.prot == RW);
    CHECK(vm_space_find_region(&sp.vm, (uintptr_t)rw, &r) && r.prot == RW);
    /* Positive results are addresses, never confusable with an errno. */
    CHECK(ro > 0 && wo > 0 && rw > 0);
}

static void test_munmap(void)
{
    fresh();
    int64_t a = syscall_vm_mmap(&sp, 8U * P, 3, 0);
    CHECK(a > 0);
    uintptr_t base = (uintptr_t)a;
    for (unsigned i = 0; i < 8; i++) touch(&sp, base + i * P);
    consistent();

    CHECK(syscall_vm_munmap(NULL, base, P) == E(22));
    CHECK(syscall_vm_munmap(&sp, base + 1U, P) == E(22));
    CHECK(syscall_vm_munmap(&sp, base, 0) == E(22));
    CHECK(syscall_vm_munmap(&sp, base - P, P) == E(22));              /* hole */
    CHECK(syscall_vm_munmap(&sp, base, 9U * P) == E(22));             /* runs off the end */
    CHECK(syscall_vm_munmap(&sp, base, VM_SPACE_MAX_RESERVED_BYTES + 1U) == E(22));
    CHECK(npresent == 8U && freed_total == 0U && release_calls == 0); /* rejected early */
    consistent();

    /* Middle punch with length rounding (1 byte -> one page). */
    CHECK(syscall_vm_munmap(&sp, base + 3U * P, 1U) == 0);
    CHECK(freed_total == 1U && npresent == 7U && sp.vm.region_count == 2U);
    CHECK(sp.vm.reserved_bytes == 7U * P);
    consistent();
    CHECK(syscall_vm_munmap(&sp, base + 3U * P, P) == E(22));         /* already gone */

    /* Range spanning the hole is refused as a whole, with no partial effect. */
    CHECK(syscall_vm_munmap(&sp, base + 2U * P, 3U * P) == E(22));
    CHECK(npresent == 7U);
    consistent();

    /* Front trim, back trim, then the rest. */
    CHECK(syscall_vm_munmap(&sp, base, 2U * P) == 0 && freed_total == 3U);
    CHECK(syscall_vm_munmap(&sp, base + 6U * P, 2U * P) == 0 && freed_total == 5U);
    consistent();
    CHECK(syscall_vm_munmap(&sp, base + 2U * P, P) == 0);            /* head-left page */
    CHECK(syscall_vm_munmap(&sp, base + 4U * P, 2U * P) == 0);
    CHECK(npresent == 0U && freed_total == 8U && sp.vm.region_count == 0U);
    CHECK(sp.vm.reserved_bytes == 0U && sp.vm.resident_pages == 0U);

    /* Non-resident pages release nothing but still unmap. */
    a = syscall_vm_mmap(&sp, 4U * P, 3, 0); base = (uintptr_t)a;
    freed_total = 0;
    CHECK(syscall_vm_munmap(&sp, base, 4U * P) == 0 && freed_total == 0U);

    /* A mapping spanning two adjacent regions unmaps in two steps. */
    int64_t b1 = syscall_vm_mmap(&sp, 2U * P, 3, 0);
    int64_t b2 = syscall_vm_mmap(&sp, 2U * P, 3, 0);
    CHECK(b2 == b1 - 2 * (int64_t)P);
    touch(&sp, (uintptr_t)b2); touch(&sp, (uintptr_t)b1 + P);
    release_calls = 0;
    CHECK(syscall_vm_munmap(&sp, (uintptr_t)b2, 4U * P) == 0);
    CHECK(release_calls == 2 && npresent == 0U && sp.vm.region_count == 0U);
    consistent();

    /* Failure injection. First step fails: EINVAL, nothing changed. */
    b1 = syscall_vm_mmap(&sp, 2U * P, 3, 0);
    b2 = syscall_vm_mmap(&sp, 2U * P, 3, 0);
    touch(&sp, (uintptr_t)b2); touch(&sp, (uintptr_t)b1);
    release_calls = 0; fail_after = 1;
    CHECK(syscall_vm_munmap(&sp, (uintptr_t)b2, 4U * P) == E(22));
    CHECK(npresent == 2U && sp.vm.region_count == 2U);
    consistent();
    /* Second step fails after the first was applied: EIO, state stays coherent. */
    release_calls = 0; fail_after = 2;
    CHECK(syscall_vm_munmap(&sp, (uintptr_t)b2, 4U * P) == E(5));
    CHECK(sp.vm.region_count == 1U && npresent == 1U);
    consistent();
    fail_after = 0;
    CHECK(syscall_vm_munmap(&sp, (uintptr_t)b1, 2U * P) == 0 && npresent == 0U);

    /* The image, stack and heap can never be unmapped. */
    fresh(); add_image();
    CHECK(vm_space_add_region(&sp.vm, TOP - 4U * P, 4U * P, RW, VM_REGION_STACK, VM_BACKING_ZERO));
    CHECK(syscall_vm_munmap(&sp, PAGING_USER_BASE, P) == E(22));
    CHECK(syscall_vm_munmap(&sp, PAGING_USER_BASE + 3U * P, P) == E(22));
    CHECK(syscall_vm_munmap(&sp, TOP - P, P) == E(22));
    CHECK(syscall_vm_brk(&sp, PAGING_USER_BASE + 5U * P + 1U) == (int64_t)(PAGING_USER_BASE + 5U * P + 1U));
    CHECK(syscall_vm_munmap(&sp, PAGING_USER_BASE + 5U * P, P) == E(22));
    CHECK(release_calls == 0);
}

static void test_brk(void)
{
    fresh();
    CHECK(syscall_vm_brk(NULL, 0) == E(22));
    CHECK(syscall_vm_brk(&sp, 0) == E(12));               /* no image to anchor to */
    add_image();
    const uintptr_t base = PAGING_USER_BASE + 5U * P;

    CHECK(syscall_vm_brk(&sp, 0) == (int64_t)base);       /* query */
    CHECK(syscall_vm_brk(&sp, base) == (int64_t)base);
    CHECK(syscall_vm_brk(&sp, base - 1U) == (int64_t)base);            /* below base */
    CHECK(syscall_vm_brk(&sp, base + VM_BRK_MAX_BYTES + 1U) == (int64_t)base);
    CHECK(syscall_vm_brk(&sp, (uint64_t)-1) == (int64_t)base);
    CHECK(sp.vm.region_count == 2U);

    CHECK(syscall_vm_brk(&sp, base + 3U * P + 7U) == (int64_t)(base + 3U * P + 7U));
    CHECK(syscall_vm_brk(&sp, 0) == (int64_t)(base + 3U * P + 7U));
    for (unsigned i = 0; i < 4; i++) touch(&sp, base + i * P);
    consistent();

    /* Shrink inside the last page frees nothing. */
    CHECK(syscall_vm_brk(&sp, base + 3U * P + 1U) == (int64_t)(base + 3U * P + 1U));
    CHECK(freed_total == 0U && npresent == 4U);
    consistent();
    /* Shrink across pages frees the dropped pages and keeps the rest. */
    CHECK(syscall_vm_brk(&sp, base + P + 5U) == (int64_t)(base + P + 5U));
    CHECK(freed_total == 2U && npresent == 2U);
    consistent();
    vm_region_t h;
    CHECK(vm_space_find_region(&sp.vm, base, &h) && h.end == base + 2U * P && h.resident_pages == 2U);

    /* A failed release leaves the break and the heap exactly as they were. */
    release_calls = 0; fail_after = 1;
    CHECK(syscall_vm_brk(&sp, base) == (int64_t)(base + P + 5U));
    CHECK(npresent == 2U);
    consistent();
    fail_after = 0;

    /* Back to base releases the whole heap; growing again starts clean. */
    CHECK(syscall_vm_brk(&sp, base) == (int64_t)base);
    CHECK(npresent == 0U && !vm_space_find_region(&sp.vm, base, &h));
    consistent();
    CHECK(sp.vm.reserved_bytes == 5U * P);
    CHECK(syscall_vm_brk(&sp, base + 10U) == (int64_t)(base + 10U));

    /* brk and mmap coexist: mmap addresses stay above the heap window. */
    int64_t m = syscall_vm_mmap(&sp, P, 3, 0);
    CHECK(m > 0 && (uintptr_t)m >= VM_MMAP_WINDOW_LOW);
    CHECK(syscall_vm_brk(&sp, VM_MMAP_WINDOW_LOW + 1U) == (int64_t)(base + 10U));
}

int main(void)
{
    test_mmap();
    test_munmap();
    test_brk();
    puts("syscall vm tests passed");
    return 0;
}
