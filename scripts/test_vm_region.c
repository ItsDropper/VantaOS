/*
 * Host tests for the demand-paging region table and fault classifier
 * (mm/vm_region.c). The logic is hardware-free, so it is exercised here
 * exhaustively, including concurrent insertion from several threads.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "paging.h"
#include "vm_region.h"

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    exit(1); } } while (0)

#define RW (VM_PROT_READ | VM_PROT_WRITE)
#define B ((uintptr_t)PAGING_USER_BASE)
#define P 4096U

static void test_validation(void)
{
    CHECK(vm_region_validate(B, P, RW, VM_REGION_ANON, VM_BACKING_ZERO));
    /* Alignment, zero length, overflow, boundaries. */
    CHECK(!vm_region_validate(B + 1U, P, RW, VM_REGION_ANON, VM_BACKING_ZERO));
    CHECK(!vm_region_validate(B, P + 1U, RW, VM_REGION_ANON, VM_BACKING_ZERO));
    CHECK(!vm_region_validate(B, 0U, RW, VM_REGION_ANON, VM_BACKING_ZERO));
    CHECK(!vm_region_validate(B - P, P, RW, VM_REGION_ANON, VM_BACKING_ZERO));
    CHECK(!vm_region_validate(0U, P, RW, VM_REGION_ANON, VM_BACKING_ZERO));
    CHECK(!vm_region_validate((uintptr_t)PAGING_USER_END + 1U, P, RW,
                              VM_REGION_ANON, VM_BACKING_ZERO));
    CHECK(!vm_region_validate((uintptr_t)PAGING_USER_END + 1U - P, 2U * P, RW,
                              VM_REGION_ANON, VM_BACKING_ZERO));
    CHECK(!vm_region_validate(B, (size_t)-4096, RW, VM_REGION_ANON,
                              VM_BACKING_ZERO));       /* wraps */
    CHECK(!vm_region_validate(0xFFFF800000000000ULL, P, RW, VM_REGION_ANON,
                              VM_BACKING_ZERO));       /* kernel half */
    CHECK(!vm_region_validate(B, VM_SPACE_MAX_RESERVED_BYTES + P, RW,
                              VM_REGION_ANON, VM_BACKING_ZERO));
    /* The last user page is legal. */
    CHECK(vm_region_validate((uintptr_t)PAGING_USER_END + 1U - P, P, RW,
                             VM_REGION_STACK, VM_BACKING_ZERO));
    /* W^X and type/backing/permission consistency. */
    CHECK(!vm_region_validate(B, P, VM_PROT_WRITE | VM_PROT_EXEC,
                              VM_REGION_ELF, VM_BACKING_POPULATED));
    CHECK(!vm_region_validate(B, P, VM_PROT_READ | VM_PROT_EXEC,
                              VM_REGION_ANON, VM_BACKING_ZERO));
    CHECK(!vm_region_validate(B, P, VM_PROT_READ | VM_PROT_EXEC,
                              VM_REGION_ELF, VM_BACKING_ZERO));
    CHECK(vm_region_validate(B, P, VM_PROT_READ | VM_PROT_EXEC,
                             VM_REGION_ELF, VM_BACKING_POPULATED));
    CHECK(!vm_region_validate(B, P, 0U, VM_REGION_ANON, VM_BACKING_ZERO));
    CHECK(!vm_region_validate(B, P, VM_PROT_WRITE, VM_REGION_ANON,
                              VM_BACKING_ZERO));      /* write-only */
    CHECK(!vm_region_validate(B, P, 0x8U, VM_REGION_ANON, VM_BACKING_ZERO));
    CHECK(vm_region_validate(B, P, 0U, VM_REGION_GUARD, VM_BACKING_NONE));
    CHECK(!vm_region_validate(B, P, VM_PROT_READ, VM_REGION_GUARD,
                              VM_BACKING_NONE));
    CHECK(!vm_region_validate(B, P, 0U, VM_REGION_GUARD, VM_BACKING_ZERO));
    CHECK(!vm_region_validate(B, P, RW, VM_REGION_STACK, VM_BACKING_POPULATED));
    CHECK(!vm_region_validate(B, P, RW, VM_REGION_NONE, VM_BACKING_ZERO));
}

static void test_table(void)
{
    vm_space_t space;
    vm_space_init(&space);
    CHECK(vm_space_add_region(&space, B + 4U * P, 4U * P, RW,
                              VM_REGION_ANON, VM_BACKING_ZERO));
    /* Overlaps (partial, contained, containing, identical) are refused. */
    CHECK(!vm_space_add_region(&space, B + 2U * P, 3U * P, RW,
                               VM_REGION_ANON, VM_BACKING_ZERO));
    CHECK(!vm_space_add_region(&space, B + 7U * P, 3U * P, RW,
                               VM_REGION_ANON, VM_BACKING_ZERO));
    CHECK(!vm_space_add_region(&space, B + 5U * P, P, RW,
                               VM_REGION_ANON, VM_BACKING_ZERO));
    CHECK(!vm_space_add_region(&space, B + 2U * P, 8U * P, RW,
                               VM_REGION_ANON, VM_BACKING_ZERO));
    CHECK(!vm_space_add_region(&space, B + 4U * P, 4U * P, RW,
                               VM_REGION_ANON, VM_BACKING_ZERO));
    /* Adjacent regions are fine. */
    CHECK(vm_space_add_region(&space, B + 8U * P, P, 0U, VM_REGION_GUARD,
                              VM_BACKING_NONE));
    CHECK(vm_space_add_region(&space, B + 3U * P, P, VM_PROT_READ,
                              VM_REGION_ANON, VM_BACKING_ZERO));
    vm_region_t r;
    CHECK(vm_space_find_region(&space, B + 4U * P, &r) && r.start == B + 4U * P);
    CHECK(vm_space_find_region(&space, B + 8U * P - 1U, &r) &&
          r.end == B + 8U * P);
    CHECK(vm_space_find_region(&space, B + 8U * P, &r) &&
          r.type == VM_REGION_GUARD);
    CHECK(!vm_space_find_region(&space, B + 9U * P, &r));
    CHECK(!vm_space_find_region(&space, B + 2U * P, &r));
    /* Capacity. */
    vm_space_init(&space);
    for (uint32_t i = 0; i < VM_REGION_MAX; i++)
        CHECK(vm_space_add_region(&space, B + (uintptr_t)i * 2U * P, P, RW,
                                  VM_REGION_ANON, VM_BACKING_ZERO));
    CHECK(!vm_space_add_region(&space, B + 100U * P, P, RW, VM_REGION_ANON,
                               VM_BACKING_ZERO));
    vm_space_clear(&space);
    CHECK(space.region_count == 0U);
    CHECK(vm_space_add_region(&space, B, P, RW, VM_REGION_ANON,
                              VM_BACKING_ZERO));
    /* Reservation budget (guards are free). */
    vm_space_init(&space);
    CHECK(vm_space_add_region(&space, B, VM_SPACE_MAX_RESERVED_BYTES, RW,
                              VM_REGION_ANON, VM_BACKING_ZERO));
    CHECK(!vm_space_add_region(&space, B + VM_SPACE_MAX_RESERVED_BYTES, P, RW,
                               VM_REGION_ANON, VM_BACKING_ZERO));
    CHECK(vm_space_add_region(&space, B + VM_SPACE_MAX_RESERVED_BYTES, P, 0U,
                              VM_REGION_GUARD, VM_BACKING_NONE));
}

static void test_classification(void)
{
    vm_space_t space;
    vm_space_init(&space);
    const uintptr_t anon = B + 0x1000, ro = B + 0x3000, guard = B + 0x4000;
    const uintptr_t elf = B + 0x8000;
    CHECK(vm_space_add_region(&space, anon, 2U * P, RW, VM_REGION_ANON,
                              VM_BACKING_ZERO));
    CHECK(vm_space_add_region(&space, ro, P, VM_PROT_READ, VM_REGION_ANON,
                              VM_BACKING_ZERO));
    CHECK(vm_space_add_region(&space, guard, P, 0U, VM_REGION_GUARD,
                              VM_BACKING_NONE));
    CHECK(vm_space_add_region(&space, elf, P, VM_PROT_READ | VM_PROT_EXEC,
                              VM_REGION_ELF, VM_BACKING_POPULATED));
    vm_region_t r;
    const uint64_t U = VM_PF_USER;

    /* Valid non-present accesses are the only recoverable ones. */
    CHECK(vm_space_classify_fault(&space, anon, U, &r) == VM_FAULT_DEMAND_MAP);
    CHECK(r.start == anon);
    CHECK(vm_space_classify_fault(&space, anon + P + 7U, U | VM_PF_WRITE, &r) ==
          VM_FAULT_DEMAND_MAP);
    CHECK(vm_space_classify_fault(&space, ro, U, &r) == VM_FAULT_DEMAND_MAP);
    /* Region forbids the access. */
    CHECK(vm_space_classify_fault(&space, ro, U | VM_PF_WRITE, &r) ==
          VM_FAULT_ACCESS_DENIED);
    CHECK(vm_space_classify_fault(&space, anon, U | VM_PF_INSTR, &r) ==
          VM_FAULT_ACCESS_DENIED);
    CHECK(vm_space_classify_fault(&space, guard, U, &r) == VM_FAULT_GUARD);
    CHECK(vm_space_classify_fault(&space, guard, U | VM_PF_WRITE, &r) ==
          VM_FAULT_GUARD);
    /* Present pages that fault are protection violations, never remapped. */
    CHECK(vm_space_classify_fault(&space, anon, U | VM_PF_PRESENT |
                                  VM_PF_WRITE, &r) == VM_FAULT_PROTECTION);
    CHECK(vm_space_classify_fault(&space, anon, U | VM_PF_PRESENT |
                                  VM_PF_INSTR, &r) == VM_FAULT_PROTECTION);
    /* Populated regions are not demand-backed, even for a legal access. */
    CHECK(vm_space_classify_fault(&space, elf, U, &r) == VM_FAULT_NOT_DEMAND);
    CHECK(vm_space_classify_fault(&space, elf, U | VM_PF_INSTR, &r) ==
          VM_FAULT_NOT_DEMAND);
    CHECK(vm_space_classify_fault(&space, elf, U | VM_PF_WRITE, &r) ==
          VM_FAULT_ACCESS_DENIED);
    /* Unreserved, kernel, and out-of-window addresses never allocate. */
    CHECK(vm_space_classify_fault(&space, B + 0x10000, U, &r) ==
          VM_FAULT_NO_REGION);
    CHECK(vm_space_classify_fault(&space, 0x200000U, U, &r) ==
          VM_FAULT_NO_REGION);
    CHECK(vm_space_classify_fault(&space, 0xFFFF800000000000ULL, U, &r) ==
          VM_FAULT_NO_REGION);
    CHECK(vm_space_classify_fault(&space, (uintptr_t)PAGING_USER_END + 1U, U,
                                  &r) == VM_FAULT_NO_REGION);
    CHECK(vm_space_classify_fault(&space, 0U, U, &r) == VM_FAULT_NO_REGION);
    /* Reserved-bit corruption and supervisor-origin faults are never demand. */
    CHECK(vm_space_classify_fault(&space, anon, U | VM_PF_RESERVED |
                                  VM_PF_PRESENT, &r) == VM_FAULT_RESERVED_BIT);
    CHECK(vm_space_classify_fault(&space, anon, 0U, &r) ==
          VM_FAULT_NOT_USER_MODE);
    /* An empty space resolves nothing. */
    vm_space_t empty;
    vm_space_init(&empty);
    CHECK(vm_space_classify_fault(&empty, anon, U, &r) == VM_FAULT_NO_REGION);
    CHECK(vm_space_classify_fault(NULL, anon, U, &r) == VM_FAULT_NO_REGION);
}

static void test_accounting(void)
{
    vm_space_t space;
    vm_space_init(&space);
    CHECK(vm_space_add_region(&space, B, 4U * P, RW, VM_REGION_ANON,
                              VM_BACKING_ZERO));
    vm_space_note_resident(&space, B, 1);
    vm_space_note_resident(&space, B + P, 1);
    CHECK(space.resident_pages == 2U && space.demand_faults == 2U);
    CHECK(space.regions[0].resident_pages == 2U);
    vm_space_note_resident(&space, B, -1);
    CHECK(space.resident_pages == 1U && space.demand_faults == 2U);
    vm_space_note_resident(&space, B + 100U * P, 1);   /* outside: ignored */
    CHECK(space.resident_pages == 1U);
}

#define THREADS 8
static vm_space_t shared_space;
static int added[THREADS];

static void* add_worker(void* arg)
{
    int id = (int)(intptr_t)arg;
    /* All threads race for the same 4 slots of address space. */
    for (int slot = 0; slot < 4; slot++)
        if (vm_space_add_region(&shared_space, B + (uintptr_t)slot * 4U * P,
                                4U * P, RW, VM_REGION_ANON, VM_BACKING_ZERO))
            added[id]++;
    vm_region_t r;
    for (int i = 0; i < 20000; i++)
        (void)vm_space_find_region(&shared_space, B + (uintptr_t)(i % 16) * P,
                                   &r);
    return NULL;
}

static void test_concurrency(void)
{
    vm_space_init(&shared_space);
    pthread_t threads[THREADS];
    for (intptr_t i = 0; i < THREADS; i++)
        CHECK(pthread_create(&threads[i], NULL, add_worker, (void*)i) == 0);
    for (int i = 0; i < THREADS; i++)
        pthread_join(threads[i], NULL);
    int total = 0;
    for (int i = 0; i < THREADS; i++)
        total += added[i];
    CHECK(total == 4);                       /* exactly one winner per slot */
    CHECK(shared_space.region_count == 4U);
    for (uint32_t i = 0; i < shared_space.region_count; i++)
        for (uint32_t j = i + 1U; j < shared_space.region_count; j++)
            CHECK(shared_space.regions[i].end <= shared_space.regions[j].start ||
                  shared_space.regions[j].end <= shared_space.regions[i].start);
}

int main(void)
{
    test_validation();
    test_table();
    test_classification();
    test_accounting();
    test_concurrency();
    puts("vm region tests passed");
    return 0;
}
