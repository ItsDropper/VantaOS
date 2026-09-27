#include "process.h"
#include "heap.h"

static process_t processes[PROCESS_MAX];

static int initialized = 0;
static uint32_t current_pid = 0;
static uint32_t scheduler_cursor = 1;

static void process_copy_name(
    char* destination,
    const char* source
)
{
    unsigned int i = 0;

    while (source != 0 &&
           source[i] != 0 &&
           i < PROCESS_NAME_MAX)
    {
        destination[i] = source[i];
        i++;
    }

    destination[i] = 0;
}

static int process_slot_available(uint32_t pid)
{
    return processes[pid].state == PROCESS_UNUSED ||
           processes[pid].state == PROCESS_TERMINATED;
}

static int process_allocate_kernel_stack(process_t* process)
{
    if (!process || !heap_is_initialized())
        return 0;

    void* stack = kmalloc(PROCESS_STACK_SIZE);

    if (!stack)
        return 0;

    uintptr_t base = (uintptr_t)stack;
    uintptr_t top = base + PROCESS_STACK_SIZE;

    if (top <= base ||
        top > 0xFFFFFFFFU)
    {
        kfree(stack);
        return 0;
    }

    process->kernel_stack_base = base;
    process->kernel_stack_top = top;
    process->stack_pointer =
        (uint32_t)(top & ~(uintptr_t)0xFU);

    return 1;
}

void process_initialize(void)
{
    initialized = 0;
    current_pid = 0;
    scheduler_cursor = 1;

    for (unsigned int i = 0; i < PROCESS_MAX; i++)
    {
        processes[i].pid = i;
        processes[i].parent_pid = 0;
        processes[i].state = PROCESS_UNUSED;
        processes[i].name[0] = 0;
        processes[i].stack_pointer = 0;
        processes[i].kernel_stack_base = 0;
        processes[i].kernel_stack_top = 0;
        processes[i].entry = 0;
    }

    initialized = 1;
}

int process_is_initialized(void)
{
    return initialized;
}

int process_attach_current(
    const char* name,
    uint32_t parent_pid
)
{
    if (!initialized ||
        name == 0 ||
        name[0] == 0)
        return -1;

    for (uint32_t pid = 1; pid < PROCESS_MAX; pid++)
    {
        if (!process_slot_available(pid))
            continue;

        uint32_t current_stack;

        __asm__ volatile (
            "mov %%esp, %0"
            : "=r"(current_stack)
        );

        processes[pid].pid = pid;
        processes[pid].parent_pid = parent_pid;
        processes[pid].state = PROCESS_RUNNING;
        processes[pid].stack_pointer = current_stack;
        processes[pid].kernel_stack_base = 0;
        processes[pid].kernel_stack_top = 0;
        processes[pid].entry = 0;

        process_copy_name(
            processes[pid].name,
            name
        );

        current_pid = pid;
        scheduler_cursor = pid + 1;

        if (scheduler_cursor >= PROCESS_MAX)
            scheduler_cursor = 1;

        return (int)pid;
    }

    return -1;
}

int process_create_kernel(
    const char* name,
    uint32_t parent_pid,
    process_entry_t entry
)
{
    if (!initialized ||
        name == 0 ||
        name[0] == 0 ||
        entry == 0)
        return -1;

    for (uint32_t pid = 1; pid < PROCESS_MAX; pid++)
    {
        if (!process_slot_available(pid))
            continue;

        processes[pid].pid = pid;
        processes[pid].parent_pid = parent_pid;
        processes[pid].state = PROCESS_READY;
        processes[pid].stack_pointer = 0;
        processes[pid].kernel_stack_base = 0;
        processes[pid].kernel_stack_top = 0;
        processes[pid].entry = entry;
        processes[pid].name[0] = 0;

        if (!process_allocate_kernel_stack(
                &processes[pid]))
        {
            processes[pid].state = PROCESS_UNUSED;
            return -1;
        }

        process_copy_name(
            processes[pid].name,
            name
        );

        return (int)pid;
    }

    return -1;
}

int process_set_running(uint32_t pid)
{
    if (!initialized ||
        pid >= PROCESS_MAX)
        return 0;

    if (processes[pid].state != PROCESS_READY &&
        processes[pid].state != PROCESS_RUNNING)
        return 0;

    current_pid = pid;
    processes[pid].state = PROCESS_RUNNING;

    for (uint32_t i = 1; i < PROCESS_MAX; i++)
    {
        if (i != pid &&
            processes[i].state == PROCESS_RUNNING)
            processes[i].state = PROCESS_READY;
    }

    scheduler_cursor = pid + 1;

    if (scheduler_cursor >= PROCESS_MAX)
        scheduler_cursor = 1;

    return 1;
}

int process_wake(uint32_t pid)
{
    if (!initialized ||
        pid >= PROCESS_MAX)
        return 0;

    if (processes[pid].state != PROCESS_SLEEPING)
        return 0;

    processes[pid].state = PROCESS_READY;
    return 1;
}

void process_block_current(void)
{
    if (!initialized ||
        current_pid >= PROCESS_MAX)
        return;

    if (processes[current_pid].state ==
        PROCESS_RUNNING)
    {
        processes[current_pid].state =
            PROCESS_SLEEPING;
    }
}

uint32_t process_pick_next(void)
{
    if (!initialized)
        return 0;

    for (uint32_t offset = 0;
         offset < PROCESS_MAX - 1;
         offset++)
    {
        uint32_t pid =
            scheduler_cursor + offset;

        while (pid >= PROCESS_MAX)
            pid -= PROCESS_MAX;

        if (pid == 0)
            continue;

        if (processes[pid].state ==
            PROCESS_READY)
            return pid;
    }

    return current_pid;
}

uint32_t process_schedule(uint32_t current_stack)
{
    /*
     * Scheduler selection is now deterministic, but the actual
     * register/stack switch remains disabled until its assembly
     * frame is implemented and verified.
     */
    (void)process_pick_next();
    return current_stack;
}

const process_t* process_get(uint32_t pid)
{
    if (!initialized ||
        pid >= PROCESS_MAX)
        return 0;

    if (processes[pid].state == PROCESS_UNUSED)
        return 0;

    return &processes[pid];
}

unsigned int process_count(void)
{
    if (!initialized)
        return 0;

    unsigned int count = 0;

    for (unsigned int i = 0;
         i < PROCESS_MAX;
         i++)
    {
        if (processes[i].state != PROCESS_UNUSED &&
            processes[i].state != PROCESS_TERMINATED)
            count++;
    }

    return count;
}

uint32_t process_current_pid(void)
{
    return current_pid;
}
