/*
 * Host tests for the demand-paging region table and fault classifier
 * (mm/vm_region.c). The logic is hardware-free, so it is exercised here
 * exhaustively, including concurrent insertion from several threads.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void test_file_backing(void)
{
    vm_space_t space;
    uint8_t image[32];
    for (size_t i = 0U; i < sizeof(image); i++)
        image[i] = (uint8_t)(i + 1U);
    vm_region_t region;

    vm_space_init(&space);
    CHECK(!vm_space_add_region(&space, B, P, RW, VM_REGION_ELF,
                               VM_BACKING_FILE));
    CHECK(vm_space_set_image_backing(&space, image, sizeof(image)));
    CHECK(!vm_space_set_image_backing(&space, image, sizeof(image)));
    CHECK(!vm_space_add_file_region(&space, B, P, RW, 30U, 0U, 4U));
    CHECK(!vm_space_add_file_region(&space, B, P, VM_PROT_READ | VM_PROT_EXEC,
                                    0U, 0U, 8U));
    CHECK(vm_space_add_file_region(&space, B, P, RW, 8U, 4U, 16U));
    CHECK(vm_space_find_region(&space, B, &region));
    CHECK(region.backing == VM_BACKING_FILE && region.file_data == image &&
          region.file_offset == 8U && region.file_prefix == 4U &&
          region.file_bytes == 16U);
    CHECK(vm_space_classify_fault(&space, B + 8U,
                                  VM_PF_USER | VM_PF_WRITE, &region) ==
          VM_FAULT_DEMAND_MAP);
    CHECK(region.file_data[region.file_offset] == image[8]);
    CHECK(!vm_space_add_file_region(&space, B, P, RW, 0U, 0U, 8U));
    CHECK(space.region_count == 1U && space.reserved_bytes == P);
    vm_space_clear(&space);
    CHECK(space.image_backing == NULL && space.image_backing_size == 0U);
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


/* ---- mmap / munmap / brk ---------------------------------------------- */

#define TOP ((uintptr_t)PAGING_USER_END + 1U)
#define WLOW ((uintptr_t)VM_MMAP_WINDOW_LOW)

static void test_map_anon(void)
{
    vm_space_t s;
    uintptr_t a, b, c;
    vm_space_init(&s);

    /* First mapping lands at the very top; the next sits directly below it. */
    CHECK(vm_space_map_anon(&s, P, RW, &a) && a == TOP - P);
    CHECK(vm_space_map_anon(&s, 3U * P, RW, &b) && b == a - 3U * P);
    /* Length is rounded up; read-only is fine; W^X and bad prot are not. */
    CHECK(vm_space_map_anon(&s, P + 1U, VM_PROT_READ, &c) && c == b - 2U * P);
    CHECK(!vm_space_map_anon(&s, P, VM_PROT_READ | VM_PROT_EXEC, &c));
    CHECK(!vm_space_map_anon(&s, P, VM_PROT_WRITE, &c));
    CHECK(!vm_space_map_anon(&s, P, 0U, &c));
    CHECK(!vm_space_map_anon(&s, 0U, RW, &c));
    CHECK(!vm_space_map_anon(&s, (size_t)-1, RW, &c));          /* overflow */
    CHECK(!vm_space_map_anon(&s, VM_SPACE_MAX_RESERVED_BYTES + P, RW, &c));
    CHECK(!vm_space_map_anon(&s, P, RW, NULL) && !vm_space_map_anon(NULL, P, RW, &c));
    CHECK(s.region_count == 3U);                    /* failures changed nothing */

    /* The new mapping is an ordinary anon region the fault classifier knows. */
    vm_region_t r;
    CHECK(vm_space_find_region(&s, a, &r) && r.type == VM_REGION_ANON &&
          r.backing == VM_BACKING_ZERO && r.prot == RW && r.flags == 0U);
    CHECK(vm_space_classify_fault(&s, a, VM_PF_USER | VM_PF_WRITE, &r) ==
          VM_FAULT_DEMAND_MAP);

    /* Mappings stay below the stack reservation and never touch it. */
    vm_space_init(&s);
    uintptr_t stack = TOP - 64U * P;
    CHECK(vm_space_add_region(&s, stack, 64U * P, RW, VM_REGION_STACK, VM_BACKING_ZERO));
    CHECK(vm_space_add_region(&s, stack - P, P, 0U, VM_REGION_GUARD, VM_BACKING_NONE));
    CHECK(vm_space_map_anon(&s, P, RW, &a) && a == stack - 2U * P);

    /* Window exhaustion: only the gap between WLOW and the guard remains. */
    vm_space_init(&s);
    CHECK(vm_space_add_region(&s, WLOW + 2U * P, P, 0U, VM_REGION_GUARD, VM_BACKING_NONE));
    CHECK(!vm_space_map_anon(&s, 3U * P, RW, &a));   /* only 2 pages fit below */
    CHECK(vm_space_map_anon(&s, 2U * P, RW, &a) && a == WLOW);
    CHECK(!vm_space_map_anon(&s, P, RW, &a));

    /* Region slots and reservation budget are enforced without side effects. */
    vm_space_init(&s);
    for (uint32_t i = 0; i < VM_REGION_MAX; i++)
        CHECK(vm_space_map_anon(&s, P, RW, &a));
    CHECK(!vm_space_map_anon(&s, P, RW, &a) && s.region_count == VM_REGION_MAX);
    vm_space_init(&s);
    CHECK(vm_space_map_anon(&s, VM_SPACE_MAX_RESERVED_BYTES, RW, &a));
    CHECK(!vm_space_map_anon(&s, P, RW, &a));
}

static void test_unmap_shapes(void)
{
    vm_space_t s;
    uintptr_t a;
    vm_unmap_step_t st;
    vm_region_t r;
    vm_space_init(&s);
    CHECK(vm_space_map_anon(&s, 8U * P, RW, &a));       /* 8-page region */

    /* Argument validation. */
    CHECK(!vm_space_unmap_check(&s, a + 1U, P));
    CHECK(!vm_space_unmap_check(&s, a, P + 1U));
    CHECK(!vm_space_unmap_check(&s, a, 0U));
    CHECK(!vm_space_unmap_check(&s, a, (size_t)-4096));
    CHECK(!vm_space_unmap_check(NULL, a, P));
    CHECK(!vm_space_unmap_check(&s, a - P, P));                   /* hole */
    CHECK(!vm_space_unmap_check(&s, a, 9U * P));                  /* past end */
    CHECK(!vm_space_unmap_check(&s, PAGING_USER_BASE - P, P));

    /* Classify each shape. */
    CHECK(vm_space_unmap_check(&s, a + 2U * P, 3U * P));
    CHECK(vm_space_unmap_next(&s, a + 2U * P, a + 5U * P, &st) && st.kind == VM_UNMAP_SPLIT);
    CHECK(vm_space_unmap_next(&s, a, a + 3U * P, &st) && st.kind == VM_UNMAP_FRONT);
    CHECK(vm_space_unmap_next(&s, a + 5U * P, a + 8U * P, &st) && st.kind == VM_UNMAP_BACK);
    CHECK(vm_space_unmap_next(&s, a, a + 8U * P, &st) && st.kind == VM_UNMAP_FULL);
    CHECK(!vm_space_unmap_next(&s, a - P, a, &st));                /* no region */

    /* Commit rejects impossible resident counts and stale steps, atomically. */
    CHECK(vm_space_unmap_next(&s, a + 2U * P, a + 5U * P, &st));
    CHECK(!vm_space_unmap_commit(&s, &st, 3U, 0U));      /* head only 2 pages */
    CHECK(!vm_space_unmap_commit(&s, &st, 0U, 4U));      /* tail only 3 pages */
    CHECK(s.region_count == 1U && s.reserved_bytes == 8U * P);
    vm_unmap_step_t stale = st; stale.original.end -= P;
    CHECK(!vm_space_unmap_commit(&s, &stale, 0U, 0U));
    CHECK(!vm_space_unmap_commit(&s, NULL, 0U, 0U));

    /* Punch a hole: head and tail survive, accounting follows. */
    for (uint32_t i = 0; i < 8U; i++) vm_space_note_resident(&s, a + i * P, 1);
    CHECK(s.resident_pages == 8U);
    CHECK(vm_space_unmap_commit(&s, &st, 2U, 3U));       /* 3 pages released */
    CHECK(s.region_count == 2U && s.reserved_bytes == 5U * P && s.resident_pages == 5U);
    CHECK(vm_space_find_region(&s, a, &r) && r.end == a + 2U * P && r.resident_pages == 2U);
    CHECK(vm_space_find_region(&s, a + 5U * P, &r) && r.start == a + 5U * P &&
          r.end == a + 8U * P && r.resident_pages == 3U);
    CHECK(!vm_space_find_region(&s, a + 3U * P, &r));
    /* The hole faults as no-region now. */
    CHECK(vm_space_classify_fault(&s, a + 3U * P, VM_PF_USER, &r) == VM_FAULT_NO_REGION);

    /* Spanning two adjacent regions removes the tail of one and head of other. */
    CHECK(vm_space_unmap_check(&s, a + P, 6U * P) == 0);          /* crosses the hole */
    CHECK(vm_space_unmap_check(&s, a + P, P));
    CHECK(vm_space_unmap_next(&s, a + P, a + 2U * P, &st) && st.kind == VM_UNMAP_BACK);
    CHECK(vm_space_unmap_commit(&s, &st, 1U, 0U));
    CHECK(s.resident_pages == 4U && s.reserved_bytes == 4U * P);

    /* Middle punch with the table full is refused up front, no side effects. */
    vm_space_init(&s);
    CHECK(vm_space_map_anon(&s, 8U * P, RW, &a));
    for (uint32_t i = 1; i < VM_REGION_MAX; i++) {
        uintptr_t t;
        CHECK(vm_space_map_anon(&s, P, RW, &t));
    }
    CHECK(s.region_count == VM_REGION_MAX);
    CHECK(!vm_space_unmap_check(&s, a + 2U * P, P));
    CHECK(vm_space_unmap_check(&s, a, P));              /* front trim still fine */
    CHECK(s.region_count == VM_REGION_MAX);

    /* Non-anon regions and the heap can never be unmapped. */
    vm_space_init(&s);
    CHECK(vm_space_add_region(&s, PAGING_USER_BASE, 2U * P, VM_PROT_READ | VM_PROT_EXEC,
                              VM_REGION_ELF, VM_BACKING_POPULATED));
    CHECK(vm_space_add_region(&s, TOP - 4U * P, 4U * P, RW, VM_REGION_STACK, VM_BACKING_ZERO));
    CHECK(!vm_space_unmap_check(&s, PAGING_USER_BASE, P));
    CHECK(!vm_space_unmap_check(&s, TOP - P, P));
    CHECK(vm_space_brk_grow(&s, PAGING_USER_BASE + 2U * P + 100U));
    CHECK(!vm_space_unmap_check(&s, PAGING_USER_BASE + 2U * P, P));
}

static void test_brk(void)
{
    vm_space_t s;
    uintptr_t base, cur, rs, re;
    const uintptr_t img = PAGING_USER_BASE;
    vm_space_init(&s);

    /* No image: brk cannot initialize. */
    CHECK(!vm_space_brk_get(&s, &base, &cur));
    CHECK(!vm_space_brk_grow(&s, img + 10U * P));

    CHECK(vm_space_add_region(&s, img, 3U * P, VM_PROT_READ | VM_PROT_EXEC,
                              VM_REGION_ELF, VM_BACKING_POPULATED));
    CHECK(vm_space_add_region(&s, img + 3U * P, 2U * P, RW, VM_REGION_ELF, VM_BACKING_ZERO));
    CHECK(vm_space_brk_get(&s, &base, &cur) && base == img + 5U * P && cur == base);
    CHECK(s.region_count == 2U);                       /* no heap region yet */

    /* Growth rules: not backwards, not past the window or the cap. */
    CHECK(!vm_space_brk_grow(&s, base) && !vm_space_brk_grow(&s, base - 1U));
    CHECK(!vm_space_brk_grow(&s, base + VM_BRK_MAX_BYTES + 1U));
    CHECK(!vm_space_brk_grow(&s, (uintptr_t)-1));
    CHECK(s.region_count == 2U && s.brk_cur == base);

    /* Growing by a few bytes maps exactly one page; further growth in-page is free. */
    CHECK(vm_space_brk_grow(&s, base + 10U));
    vm_region_t r;
    CHECK(vm_space_find_region(&s, base, &r) && r.type == VM_REGION_ANON &&
          r.flags == VM_REGION_FLAG_HEAP && r.end == base + P);
    CHECK(vm_space_brk_grow(&s, base + 4000U) && r.end == base + P);
    vm_space_find_region(&s, base, &r); CHECK(r.end == base + P);
    CHECK(vm_space_brk_get(&s, NULL, &cur) && cur == base + 4000U);
    CHECK(vm_space_brk_grow(&s, base + P) && s.brk_cur == base + P);        /* exact page */
    vm_space_find_region(&s, base, &r); CHECK(r.end == base + P);
    CHECK(vm_space_brk_grow(&s, base + P + 1U));                            /* next page */
    vm_space_find_region(&s, base, &r); CHECK(r.end == base + 2U * P);
    CHECK(s.reserved_bytes == 5U * P + 2U * P);

    /* Growth is blocked by anything in the way, without side effects. */
    vm_space_t t;
    vm_space_init(&t);
    CHECK(vm_space_add_region(&t, img, P, VM_PROT_READ | VM_PROT_EXEC, VM_REGION_ELF, VM_BACKING_POPULATED));
    CHECK(vm_space_brk_grow(&t, img + P + 1U));                              /* heap = 1 page */
    CHECK(vm_space_add_region(&t, img + 3U * P, P, 0U, VM_REGION_GUARD, VM_BACKING_NONE));
    CHECK(vm_space_brk_grow(&t, img + 3U * P));                              /* up to the guard */
    CHECK(!vm_space_brk_grow(&t, img + 3U * P + 1U));
    CHECK(t.brk_cur == img + 3U * P);

    /* Shrink: plan, release (here: pretend 2 resident), commit. */
    vm_space_note_resident(&s, base, 1); vm_space_note_resident(&s, base + P, 1);
    CHECK(!vm_space_brk_shrink_plan(&s, base + 2U * P, &rs, &re));          /* not a shrink */
    CHECK(!vm_space_brk_shrink_plan(&s, base - 1U, &rs, &re));              /* below base */
    CHECK(vm_space_brk_shrink_plan(&s, base + 100U, &rs, &re) &&
          rs == base + P && re == base + 2U * P);
    CHECK(vm_space_brk_shrink_commit(&s, base + 100U, 3U) == 0);            /* > 1 page */
    CHECK(vm_space_brk_shrink_commit(&s, base + 100U, 1U));
    CHECK(s.brk_cur == base + 100U && s.resident_pages == 1U &&
          s.reserved_bytes == 5U * P + P);
    vm_space_find_region(&s, base, &r); CHECK(r.end == base + P && r.resident_pages == 1U);

    /* Shrinking inside the last page releases nothing. */
    CHECK(vm_space_brk_shrink_plan(&s, base + 50U, &rs, &re) && rs == re);
    CHECK(vm_space_brk_shrink_commit(&s, base + 50U, 1U) && s.brk_cur == base + 50U);

    /* Shrinking to base drops the whole heap region; growing again re-creates it. */
    CHECK(vm_space_brk_shrink_plan(&s, base, &rs, &re) && rs == base && re == base + P);
    CHECK(vm_space_brk_shrink_commit(&s, base, 0U));
    CHECK(!vm_space_find_region(&s, base, &r) && s.brk_cur == base);
    CHECK(s.reserved_bytes == 5U * P && s.resident_pages == 0U);
    CHECK(vm_space_brk_grow(&s, base + 1U) && vm_space_find_region(&s, base, &r));

    /* The heap and mmap never collide: mmap lands in the window, not the heap. */
    uintptr_t m;
    CHECK(vm_space_map_anon(&s, P, RW, &m) && m >= WLOW);
    CHECK(!vm_space_brk_grow(&s, WLOW + 1U));
}

/* Model-based test: random mmap/touch/munmap checked against a per-page model. */
#define MODEL_PAGES (1U << 14)                       /* 64 MiB below TOP */
static uint8_t model[MODEL_PAGES];                   /* 0 none, 1 mapped, 2 resident */

static size_t midx(uintptr_t a) { return (size_t)((TOP - a) / P) - 1U; }

static void model_check(vm_space_t* s)
{
    uint64_t mapped = 0, resident = 0;
    for (size_t i = 0; i < MODEL_PAGES; i++) {
        uintptr_t a = TOP - (i + 1U) * P;
        vm_region_t r;
        int have = vm_space_find_region(s, a, &r);
        CHECK(have == (model[i] != 0));
        if (model[i]) { mapped++; if (model[i] == 2) resident++; }
    }
    CHECK(s->reserved_bytes == mapped * P);
    CHECK(s->resident_pages == resident);
    uint64_t sum = 0;
    for (uint32_t i = 0; i < s->region_count; i++) {
        const vm_region_t* r = &s->regions[i];
        CHECK(r->start < r->end && (r->start % P) == 0 && (r->end % P) == 0);
        uint32_t res = 0;
        for (uintptr_t a = r->start; a < r->end; a += P) res += model[midx(a)] == 2;
        CHECK(r->resident_pages == res);
        sum += res;
        for (uint32_t j = i + 1U; j < s->region_count; j++)
            CHECK(!(r->start < s->regions[j].end && s->regions[j].start < r->end));
    }
    CHECK(sum == resident && s->region_count <= VM_REGION_MAX);
}

static void test_model(unsigned seed)
{
    vm_space_t s;
    uint32_t x = seed * 2654435761U + 1U;
#define RND() (x ^= x << 13, x ^= x >> 17, x ^= x << 5, x)
    vm_space_init(&s);
    memset(model, 0, sizeof(model));

    for (int op = 0; op < 4000; op++) {
        uint32_t k = RND() % 10U;
        if (k < 3U) {                                          /* mmap */
            size_t pages = 1U + RND() % 12U;
            uintptr_t a;
            int ok = vm_space_map_anon(&s, pages * P - (RND() % 3U ? 0U : 17U), RW, &a);
            if (ok) {
                CHECK(a >= WLOW && a + pages * P <= TOP);
                CHECK(TOP - a <= MODEL_PAGES * P);             /* stayed inside the model */
                for (size_t i = 0; i < pages; i++) {
                    CHECK(model[midx(a + i * P)] == 0);        /* really was free */
                    model[midx(a + i * P)] = 1;
                }
            } else CHECK(s.region_count == VM_REGION_MAX);
        } else if (k < 6U) {                                   /* touch (fault) */
            uintptr_t a = TOP - (1U + RND() % 400U) * P;
            vm_region_t r;
            if (vm_space_find_region(&s, a, &r) && model[midx(a)] == 1) {
                CHECK(vm_space_classify_fault(&s, a, VM_PF_USER | VM_PF_WRITE, &r) ==
                      VM_FAULT_DEMAND_MAP);
                model[midx(a)] = 2;
                vm_space_note_resident(&s, a, 1);
            }
        } else {                                               /* munmap */
            uintptr_t a = TOP - (1U + RND() % 400U) * P;
            size_t pages = 1U + RND() % 10U;
            if (a < pages * P) continue;
            uintptr_t start = a - (pages - 1U) * P;
            int expect = 1;
            for (size_t i = 0; i < pages; i++)
                if (model[midx(start + i * P)] == 0) expect = 0;
            int can = vm_space_unmap_check(&s, start, pages * P);
            if (expect && !can) CHECK(s.region_count == VM_REGION_MAX);  /* only a full table */
            if (!expect) CHECK(!can);
            if (!can) continue;
            uintptr_t cur = start, end = start + pages * P;
            while (cur < end) {
                vm_unmap_step_t st;
                CHECK(vm_space_unmap_next(&s, cur, end, &st));
                CHECK(st.start == cur && st.end <= end && st.end > st.start);
                for (uintptr_t p = st.start; p < st.end; p += P) model[midx(p)] = 0;
                uint32_t l = 0, rr = 0;
                for (uintptr_t p = st.original.start; p < st.start; p += P) l += model[midx(p)] == 2;
                for (uintptr_t p = st.end; p < st.original.end; p += P) rr += model[midx(p)] == 2;
                CHECK(vm_space_unmap_commit(&s, &st, l, rr));
                cur = st.end;
            }
        }
        if (op % 97 == 0) model_check(&s);
    }
    model_check(&s);
#undef RND
}

int main(void)
{
    test_validation();
    test_table();
    test_file_backing();
    test_classification();
    test_accounting();
    test_concurrency();
    test_map_anon();
    test_unmap_shapes();
    test_brk();
    for (unsigned seed = 1U; seed <= 8U; seed++)
        test_model(seed);
    puts("vm region tests passed");
    return 0;
}
