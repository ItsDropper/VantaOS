#include "user_mode_selftest.h"

#include "cpu_local.h"
#include "gdt.h"
#include "paging.h"
#include "pmm.h"
#include "process.h"
#include "serial.h"

#include <stddef.h>
#include <stdint.h>

#define USER_TEST_KERNEL_ADDRESS 0x00100000ULL
#define USER_TEST_SYSCALL_MODE 1U
#define USER_TEST_PROTECTION_MODE 2U
#define USER_TEST_KERNEL_STACK_SIZE 16384U
#define IDENTITY_MAP_LIMIT 0x40000000ULL

extern void user_mode_test_enter(uintptr_t instruction_pointer,
                                 uintptr_t user_stack_pointer);
extern void user_mode_test_return_trampoline(void);
extern unsigned char stack_top;

uintptr_t user_mode_test_saved_rsp;
uint64_t user_mode_test_saved_rflags;

static uint8_t user_test_kernel_stack[USER_TEST_KERNEL_STACK_SIZE]
    __attribute__((aligned(16)));
static volatile uint32_t user_test_active;
static volatile uint32_t user_test_mode;
static volatile uint32_t user_test_syscall_seen;
static volatile uint32_t user_test_protection_fault_seen;
static volatile uint32_t user_test_unexpected;

static void copy_bytes(void* destination, const uint8_t* source, size_t count)
{
    uint8_t* out = (uint8_t*)destination;
    for (size_t i = 0; i < count; i++)
        out[i] = source[i];
}

static void user_test_redirect_to_kernel(struct exception_frame* frame)
{
    frame->rip = (uintptr_t)user_mode_test_return_trampoline;
    frame->cs = 0x08U;
    /* Return to a controlled kernel trampoline with maskable interrupts off. */
    frame->rflags = 0x02U;
}

void user_mode_selftest_syscall_dispatch(struct exception_frame* frame)
{
    if (!frame)
        return;

    if (__atomic_load_n(&user_test_active, __ATOMIC_ACQUIRE) == 0U ||
        (frame->cs & 3U) != 3U)
    {
        /* No general syscall ABI is exposed by this test-only IDT gate. */
        frame->rax = (uint64_t)(int64_t)-38;
        return;
    }

    const uint32_t mode =
        __atomic_load_n(&user_test_mode, __ATOMIC_ACQUIRE);
    if (mode == USER_TEST_SYSCALL_MODE && frame->rax == 1U)
        __atomic_store_n(&user_test_syscall_seen, 1U, __ATOMIC_RELEASE);
    else
        __atomic_store_n(&user_test_unexpected, 1U, __ATOMIC_RELEASE);

    user_test_redirect_to_kernel(frame);
}

int user_mode_selftest_handle_exception(unsigned int number,
                                        struct exception_frame* frame)
{
    if (!frame ||
        __atomic_load_n(&user_test_active, __ATOMIC_ACQUIRE) == 0U ||
        (frame->cs & 3U) != 3U)
        return 0;

    uintptr_t fault_address = 0;
    if (number == 14U)
        __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));

    const uint32_t mode =
        __atomic_load_n(&user_test_mode, __ATOMIC_ACQUIRE);
    if (mode == USER_TEST_PROTECTION_MODE && number == 14U &&
        fault_address == (uintptr_t)USER_TEST_KERNEL_ADDRESS &&
        (frame->error_code & 0x5U) == 0x5U)
    {
        __atomic_store_n(&user_test_protection_fault_seen, 1U,
                         __ATOMIC_RELEASE);
    }
    else
    {
        __atomic_store_n(&user_test_unexpected, 1U, __ATOMIC_RELEASE);
    }

    user_test_redirect_to_kernel(frame);
    return 1;
}

static int run_user_probe(unsigned int mode,
                          uintptr_t user_stack_top)
{
    __atomic_store_n(&user_test_syscall_seen, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&user_test_protection_fault_seen, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&user_test_unexpected, 0U, __ATOMIC_RELAXED);
    __atomic_store_n(&user_test_mode, mode, __ATOMIC_RELEASE);
    __atomic_store_n(&user_test_active, 1U, __ATOMIC_RELEASE);

    user_mode_test_enter((uintptr_t)PAGING_USER_BASE, user_stack_top);

    __atomic_store_n(&user_test_active, 0U, __ATOMIC_RELEASE);
    if (mode == USER_TEST_SYSCALL_MODE)
        return __atomic_load_n(&user_test_syscall_seen, __ATOMIC_ACQUIRE) != 0U &&
               __atomic_load_n(&user_test_unexpected, __ATOMIC_ACQUIRE) == 0U;

    return __atomic_load_n(&user_test_protection_fault_seen,
                           __ATOMIC_ACQUIRE) != 0U &&
           __atomic_load_n(&user_test_unexpected, __ATOMIC_ACQUIRE) == 0U;
}

int user_mode_selftest_run(void)
{
    paging_address_space_t* space = paging_create_address_space();
    void* code_frame = pmm_alloc_block();
    void* stack_frame = pmm_alloc_block();
    int code_mapped = 0;
    int stack_mapped = 0;
    int syscall_ok = 0;
    int isolation_ok = 0;
    int ok = space != NULL && code_frame != NULL && stack_frame != NULL;
    const uint32_t cpu_id = process_current_cpu_id();

    if (!ok || cpu_id >= CPU_LOCAL_MAX ||
        (uintptr_t)code_frame >= IDENTITY_MAP_LIMIT ||
        (uintptr_t)stack_frame >= IDENTITY_MAP_LIMIT)
        ok = 0;

    if (ok)
    {
        code_mapped = paging_map_user_page(
            space, (uintptr_t)PAGING_USER_BASE,
            (uintptr_t)code_frame, 0);
        stack_mapped = paging_map_user_page(
            space, (uintptr_t)(PAGING_USER_BASE + PAGE_SIZE),
            (uintptr_t)stack_frame, 1);
        ok = code_mapped && stack_mapped;
    }

    if (ok)
    {
        /* mov eax,1; int 0x80; ud2 -- the test-only exit syscall. */
        static const uint8_t syscall_program[] = {
            0xB8, 0x01, 0x00, 0x00, 0x00, 0xCD, 0x80, 0x0F, 0x0B
        };
        copy_bytes(code_frame, syscall_program, sizeof(syscall_program));

        if (!gdt_set_kernel_stack(
                cpu_id, (uintptr_t)user_test_kernel_stack +
                        sizeof(user_test_kernel_stack)))
            ok = 0;
    }

    if (ok)
    {
        paging_switch_address_space(space);
        ok = paging_get_current_address_space() == space;
        if (ok)
            syscall_ok = run_user_probe(
                USER_TEST_SYSCALL_MODE,
                (uintptr_t)(PAGING_USER_BASE + 2U * PAGE_SIZE - 16U));
    }

    if (space && code_mapped && stack_mapped &&
        paging_get_current_address_space() == space)
    {
        /*
         * Attempt a read from supervisor-only identity-mapped kernel memory.
         * If CPL3 isolation fails, the program reaches syscall 2 instead.
         */
        static const uint8_t protection_program[] = {
            0x48, 0xB8, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x48, 0x8B, 0x00,
            0xB8, 0x02, 0x00, 0x00, 0x00, 0xCD, 0x80, 0x0F, 0x0B
        };
        copy_bytes(code_frame, protection_program,
                   sizeof(protection_program));
        isolation_ok = run_user_probe(
            USER_TEST_PROTECTION_MODE,
            (uintptr_t)(PAGING_USER_BASE + 2U * PAGE_SIZE - 16U));
    }
    ok = ok && syscall_ok && isolation_ok;

    /* Never destroy an address space while it is loaded in CR3. */
    if (paging_get_current_address_space() == space)
        paging_switch_address_space(paging_get_kernel_address_space());

    if (space)
    {
        if (code_mapped)
            (void)paging_unmap_user_page(space, (uintptr_t)PAGING_USER_BASE);
        if (stack_mapped)
            (void)paging_unmap_user_page(
                space, (uintptr_t)(PAGING_USER_BASE + PAGE_SIZE));
        paging_destroy_address_space(space);
    }

    if (code_frame)
        pmm_free_block(code_frame);
    if (stack_frame)
        pmm_free_block(stack_frame);

    /* Restore a mapped, stable boot stack for any later privilege transition. */
    (void)gdt_set_kernel_stack(cpu_id, (uintptr_t)&stack_top);

    serial_write(syscall_ok ?
        "VANTAOS_RING3_SYSCALL_OK\n" :
        "VANTAOS_RING3_SYSCALL_FAIL\n");
    serial_write(isolation_ok ?
        "VANTAOS_RING3_KERNEL_ISOLATION_OK\n" :
        "VANTAOS_RING3_KERNEL_ISOLATION_FAIL\n");
    return ok;
}
