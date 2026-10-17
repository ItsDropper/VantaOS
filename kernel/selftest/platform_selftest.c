#include "selftest.h"

#include <stdint.h>
#include "cpu_local.h"
#include "paging.h"
#include "pmm.h"
#include "serial.h"
#include "spinlock.h"
#include "user_mode_selftest.h"
#include "../proc/process_internal.h"

#ifdef VANTAOS_SELF_TEST
static void spinlock_primitive_probe(void)
{
    spinlock_t lock = SPINLOCK_INITIALIZER;
    uint64_t flags = 0;
    int ok = spinlock_try_lock_irqsave(&lock, &flags);
    ok = !spinlock_try_lock(&lock) && ok;
    spinlock_unlock_irqrestore(&lock, flags);

    flags = 0;
    int reacquired = spinlock_try_lock_irqsave(&lock, &flags);
    ok = reacquired && ok;
    if (reacquired)
        spinlock_unlock_irqrestore(&lock, flags);

    serial_write(ok ?
        "VANTAOS_SPINLOCK_PRIMITIVE_OK\n" :
        "VANTAOS_SPINLOCK_PRIMITIVE_FAIL\n");
}

static void unpublished_image_cleanup_probe(void)
{
    const uint32_t free_before = pmm_get_free_blocks();
    paging_address_space_t* space = paging_create_address_space();
    void* code_frame = pmm_alloc_block();
    void* stack_frame = pmm_alloc_block();
    int ok = space != NULL && code_frame != NULL && stack_frame != NULL;
    int code_mapped = 0;
    int stack_mapped = 0;

    if (ok)
    {
        code_mapped = paging_map_user_page_ex(
            space, (uintptr_t)PAGING_USER_BASE,
            (uintptr_t)code_frame, 0, 1);
        stack_mapped = paging_map_user_page_ex(
            space, (uintptr_t)(PAGING_USER_BASE + PAGE_SIZE),
            (uintptr_t)stack_frame, 1, 0);
        ok = code_mapped && stack_mapped;
    }

    if (ok)
    {
        /* Destruction must fail while this CPU still has the space loaded. */
        paging_switch_address_space(space);
        /* Deterministically model a teardown blocked by an outstanding shootdown. */
        __atomic_store_n(&space->pending_shootdowns, 1U, __ATOMIC_RELEASE);
        process_release_unpublished_user_image(space, code_frame, stack_frame);
        ok = space->used != 0 &&
             paging_get_physical((uintptr_t)PAGING_USER_BASE) ==
                 (uintptr_t)code_frame &&
             paging_get_physical((uintptr_t)(PAGING_USER_BASE + PAGE_SIZE)) ==
                 (uintptr_t)stack_frame &&
             pmm_get_free_blocks() < free_before;
        __atomic_store_n(&space->pending_shootdowns, 0U, __ATOMIC_RELEASE);
        paging_switch_address_space(paging_get_kernel_address_space());

        /* Once inactive, teardown and frame release must complete. */
        process_release_unpublished_user_image(space, code_frame, stack_frame);
        ok = ok && space->used == 0 &&
             pmm_get_free_blocks() == free_before;
    }
    else
    {
        if (space)
        {
            if (paging_get_current_address_space() == space)
                paging_switch_address_space(paging_get_kernel_address_space());
            process_release_unpublished_user_image(space, code_frame, stack_frame);
        }
        else
        {
            if (code_frame)
                pmm_free_block(code_frame);
            if (stack_frame)
                pmm_free_block(stack_frame);
        }
    }

    serial_write(ok ?
        "VANTAOS_UNPUBLISHED_IMAGE_CLEANUP_OK\n" :
        "VANTAOS_UNPUBLISHED_IMAGE_CLEANUP_FAIL\n");
}

static void user_mapping_probe(void)
{
    paging_address_space_t* space = paging_create_address_space();
    void* writable_frame = pmm_alloc_block();
    void* readonly_frame = pmm_alloc_block();
    int ok = space != NULL && writable_frame != NULL &&
             readonly_frame != NULL;
    int per_cpu_ok = 0;
    const cpu_local_t* bsp_local = cpu_local_current();
    int writable_mapped = 0;
    int readonly_mapped = 0;

    if (ok)
    {
        writable_mapped = paging_map_user_page(
            space, (uintptr_t)PAGING_USER_BASE,
            (uintptr_t)writable_frame, 1);
        readonly_mapped = paging_map_user_page(
            space, (uintptr_t)(PAGING_USER_BASE + PAGE_SIZE),
            (uintptr_t)readonly_frame, 0);

        ok = writable_mapped && readonly_mapped;
        ok = !paging_map_user_page(
            space, (uintptr_t)PAGING_USER_BASE,
            (uintptr_t)writable_frame, 1) && ok;
        ok = !paging_map_user_page(
            space, (uintptr_t)(PAGING_USER_BASE + 1U),
            (uintptr_t)writable_frame, 1) && ok;
        ok = !paging_map_user_page(
            space, (uintptr_t)0x100000U,
            (uintptr_t)writable_frame, 1) && ok;
    }

    if (space && ok)
    {
        paging_switch_address_space(space);
        ok = paging_get_current_address_space() == space && ok;
        per_cpu_ok = paging_get_current_address_space() == space &&
                     bsp_local &&
                     bsp_local->current_address_space == space &&
                     (space->active_cpu_mask &
                      (1U << bsp_local->logical_id)) != 0U &&
                     space->pending_shootdowns == 0U;
        for (uint32_t cpu = 0; cpu < cpu_local_capacity(); cpu++)
        {
            const cpu_local_t* local = cpu_local_get(cpu);
            if (local && local != bsp_local)
                per_cpu_ok =
                    local->current_address_space ==
                    paging_get_kernel_address_space() && per_cpu_ok;
        }
        ok = paging_get_physical(
            (uintptr_t)(PAGING_USER_BASE + 64U)) ==
            (uintptr_t)writable_frame + 64U && ok;
        ok = (paging_get_page_flags(
            (uintptr_t)PAGING_USER_BASE) &
            (PAGE_PRESENT | PAGE_USER | PAGE_WRITE)) ==
            (PAGE_PRESENT | PAGE_USER | PAGE_WRITE) && ok;
        ok = (paging_get_page_flags(
            (uintptr_t)(PAGING_USER_BASE + PAGE_SIZE)) &
            (PAGE_PRESENT | PAGE_USER)) ==
            (PAGE_PRESENT | PAGE_USER) && ok;
        ok = (paging_get_page_flags(
            (uintptr_t)(PAGING_USER_BASE + PAGE_SIZE)) &
            PAGE_WRITE) == 0 && ok;

        if (ok)
        {
            volatile uint64_t* user_word =
                (volatile uint64_t*)(uintptr_t)(PAGING_USER_BASE + 64U);
            *user_word = 0x56414E5441555345ULL;
            ok = *user_word == 0x56414E5441555345ULL;
        }
    }

    if (space)
    {
        if (paging_get_current_address_space() == space)
        {
            if (writable_mapped)
                ok = paging_unmap_user_page(
                    space, (uintptr_t)PAGING_USER_BASE) && ok;
            if (readonly_mapped)
                ok = paging_unmap_user_page(
                    space, (uintptr_t)(PAGING_USER_BASE + PAGE_SIZE)) && ok;
            ok = paging_get_physical(
                (uintptr_t)PAGING_USER_BASE) == 0 && ok;
            paging_switch_address_space(paging_get_kernel_address_space());
            per_cpu_ok =
                paging_get_current_address_space() ==
                paging_get_kernel_address_space() &&
                space->active_cpu_mask == 0U &&
                space->pending_shootdowns == 0U && per_cpu_ok;
        }
        paging_destroy_address_space(space);
        per_cpu_ok = space->used == 0 &&
                     space->active_cpu_mask == 0U &&
                     space->pending_shootdowns == 0U && per_cpu_ok;
    }

    if (writable_frame)
        pmm_free_block(writable_frame);
    if (readonly_frame)
        pmm_free_block(readonly_frame);

    serial_write(per_cpu_ok ?
        "VANTAOS_PER_CPU_ADDRESS_SPACE_OK\n" :
        "VANTAOS_PER_CPU_ADDRESS_SPACE_FAIL\n");
    serial_write(ok ?
        "VANTAOS_USER_MAPPING_OK\n" :
        "VANTAOS_USER_MAPPING_FAIL\n");
}

void kernel_platform_selftest(void)
{
    spinlock_primitive_probe();
    int tlb_shootdown_ok = paging_tlb_shootdown(
        paging_get_kernel_address_space(), (uintptr_t)0xC0000000ULL);
    serial_write(tlb_shootdown_ok ?
        "VANTAOS_TLB_SHOOTDOWN_OK\n" :
        "VANTAOS_TLB_SHOOTDOWN_FAIL\n");
    user_mapping_probe();
    unpublished_image_cleanup_probe();
    storage_framework_selftest();
    (void)user_mode_selftest_run();
}
#endif
