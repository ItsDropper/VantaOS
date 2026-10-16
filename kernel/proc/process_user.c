#include "process_internal.h"
#include "pmm.h"

#include <stddef.h>
#include <stdint.h>

#define USER_CODE_ADDRESS ((uintptr_t)PAGING_USER_BASE)
#define USER_STACK_ADDRESS ((uintptr_t)(PAGING_USER_BASE + PAGE_SIZE))
#define USER_STACK_TOP ((uintptr_t)(PAGING_USER_BASE + 2U * PAGE_SIZE))
#define IDENTITY_MAP_LIMIT 0x40000000ULL

static void copy_image(void* destination, const void* source, size_t size)
{
    uint8_t* out = (uint8_t*)destination;
    const uint8_t* in = (const uint8_t*)source;
    for (size_t i = 0; i < size; i++)
        out[i] = in[i];
}

static void prepare_user_context(process_t* process, uintptr_t stack_top)
{
    uint64_t* stack = (uint64_t*)stack_top;
    *(--stack) = 0x1BU;            /* user SS */
    *(--stack) = USER_STACK_TOP;   /* user RSP */
    *(--stack) = 0x202U;           /* IF enabled, reserved bit set */
    *(--stack) = 0x23U;            /* user CS */
    *(--stack) = USER_CODE_ADDRESS;/* entry RIP */
    for (unsigned int i = 0; i < 15U; i++)
        *(--stack) = 0U;           /* RESTORE_ALL general registers */
    *(--stack) = 0U;               /* IRQ frame alignment padding */

    process->stack_pointer = (uintptr_t)stack;
    process->context.rsp = (uintptr_t)stack;
    process->context.rip = USER_CODE_ADDRESS;
    process->context.rflags = 0x202U;
    process->context.cs = 0x23U;
    process->context.ss = 0x1BU;
}

static void release_unpublished_user_image(
    paging_address_space_t* space, void* code_frame, void* stack_frame,
    int code_mapped, int stack_mapped)
{
    if (space)
    {
        if (code_mapped)
            (void)paging_unmap_user_page(space, USER_CODE_ADDRESS);
        if (stack_mapped)
            (void)paging_unmap_user_page(space, USER_STACK_ADDRESS);
        paging_destroy_address_space(space);
    }
    if (code_frame)
        pmm_free_block(code_frame);
    if (stack_frame)
        pmm_free_block(stack_frame);
}

int process_create_user(const char* name, uint32_t parent_pid,
                        const void* image, size_t image_size)
{
    if (!process_initialized_state || !name || !name[0] || !image ||
        image_size == 0U || image_size > PAGE_SIZE ||
        paging_get_current_address_space() !=
            paging_get_kernel_address_space())
        return -1;

    paging_address_space_t* space = paging_create_address_space();
    void* code_frame = pmm_alloc_block();
    void* stack_frame = pmm_alloc_block();
    int code_mapped = 0;
    int stack_mapped = 0;

    if (!space || !code_frame || !stack_frame ||
        (uintptr_t)code_frame >= IDENTITY_MAP_LIMIT ||
        (uintptr_t)stack_frame >= IDENTITY_MAP_LIMIT)
    {
        release_unpublished_user_image(space, code_frame, stack_frame, 0, 0);
        return -1;
    }

    copy_image(code_frame, image, image_size);
    uint8_t* stack_bytes = (uint8_t*)stack_frame;
    for (size_t i = 0; i < PAGE_SIZE; i++)
        stack_bytes[i] = 0U;

    code_mapped = paging_map_user_page(
        space, USER_CODE_ADDRESS, (uintptr_t)code_frame, 0);
    if (code_mapped)
        stack_mapped = paging_map_user_page(
            space, USER_STACK_ADDRESS, (uintptr_t)stack_frame, 1);

    if (!code_mapped || !stack_mapped)
    {
        release_unpublished_user_image(space, code_frame, stack_frame,
                                       code_mapped, stack_mapped);
        return -1;
    }

    uint64_t flags = process_table_lock_acquire();
    for (uint32_t pid = 1U; pid < PROCESS_MAX; pid++)
    {
        process_t* process = &process_table_records[pid];
        if (process->state != PROCESS_UNUSED || process_is_idle_locked(pid))
            continue;

        process_reset_record(process);
        process->parent_pid = parent_pid;
        process->owner_cpu = PROCESS_CPU_UNOWNED;
        process->affinity_cpu = process_current_cpu_id();
        process->state = PROCESS_READY;
        process->is_user = 1U;
        process->user_code_frame = code_frame;
        process->user_stack_frame = stack_frame;
        process->user_image_size = image_size;
        process->address_space = space;
        process->kernel_stack = process_stack_storage[pid];

        unsigned int i = 0;
        while (i < PROCESS_NAME_MAX && name[i])
        {
            process->name[i] = name[i];
            i++;
        }
        process->name[i] = '\0';

        uintptr_t kernel_stack_top =
            ((uintptr_t)process->kernel_stack + PROCESS_STACK_SIZE) &
            ~(uintptr_t)0x0FU;
        prepare_user_context(process, kernel_stack_top);
        process_table_lock_release(flags);
        return (int)pid;
    }

    process_table_lock_release(flags);
    release_unpublished_user_image(space, code_frame, stack_frame,
                                   code_mapped, stack_mapped);
    return -1;
}


/* Called only after the process no longer owns a CPU or the active CR3. */
int process_release_user_resources(process_t* process)
{
    if (!process)
        return 0;
    if (!process->is_user)
        return 1;
    if (!process->address_space ||
        paging_get_current_address_space() == process->address_space)
        return 0;

    if (!paging_destroy_address_space(process->address_space))
        return 0;

    if (process->user_code_frame)
        pmm_free_block(process->user_code_frame);
    if (process->user_stack_frame)
        pmm_free_block(process->user_stack_frame);

    process->address_space = NULL;
    process->user_code_frame = NULL;
    process->user_stack_frame = NULL;
    process->user_image_size = 0U;
    process->is_user = 0U;
    return 1;
}
