#include "process.h"

static process_t processes[PROCESS_MAX];

static uint8_t process_stacks[
    PROCESS_MAX
][PROCESS_STACK_SIZE]
    __attribute__((aligned(16)));

static int initialized = 0;
static uint32_t current_pid = 0;

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

static void process_entry_trampoline(void)
{
    uint32_t pid = current_pid;

    if (pid < PROCESS_MAX &&
        processes[pid].entry != 0)
    {
        processes[pid].entry();
    }

    if (pid < PROCESS_MAX)
        processes[pid].state = PROCESS_TERMINATED;

    while (1)
        __asm__ volatile ("hlt");
}

static uint32_t process_build_initial_stack(
    uint32_t pid
)
{
    uint32_t* stack =
        (uint32_t*)&process_stacks[pid][PROCESS_STACK_SIZE];

    /*
     * irq0_stub executes POPA followed by IRETD.
     *
     * POPA expects:
     *   EDI, ESI, EBP, ignored ESP, EBX, EDX, ECX, EAX
     *
     * IRETD then expects:
     *   EIP, CS, EFLAGS
     */
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;

    *--stack = (uint32_t)process_entry_trampoline;
    *--stack = 0x08;
    *--stack = 0x202;

    return (uint32_t)stack;
}

void process_initialize(void)
{
    initialized = 0;
    current_pid = 0;

    for (unsigned int i = 0; i < PROCESS_MAX; i++)
    {
        processes[i].pid = i;
        processes[i].parent_pid = 0;
        processes[i].state = PROCESS_UNUSED;
        processes[i].name[0] = 0;
        processes[i].stack_pointer = 0;
        processes[i].entry = 0;
    }

    initialized = 1;
}

int process_is_initialized(void)
{
    return initialized;
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

    for (uint32_t pid = 1;
         pid < PROCESS_MAX;
         pid++)
    {
        if (processes[pid].state != PROCESS_UNUSED &&
            processes[pid].state != PROCESS_TERMINATED)
            continue;

        processes[pid].pid = pid;
        processes[pid].parent_pid = parent_pid;
        processes[pid].state = PROCESS_READY;
        processes[pid].stack_pointer =
            process_build_initial_stack(pid);
        processes[pid].entry = entry;

        process_copy_name(
            processes[pid].name,
            name
        );

        return (int)pid;
    }

    return -1;
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

    for (uint32_t pid = 1;
         pid < PROCESS_MAX;
         pid++)
    {
        if (processes[pid].state != PROCESS_UNUSED &&
            processes[pid].state != PROCESS_TERMINATED)
            continue;

        processes[pid].pid = pid;
        processes[pid].parent_pid = parent_pid;
        processes[pid].state = PROCESS_RUNNING;
        processes[pid].stack_pointer = 0;
        processes[pid].entry = 0;

        process_copy_name(
            processes[pid].name,
            name
        );

        current_pid = pid;
        return (int)pid;
    }

    return -1;
}

int process_set_running(uint32_t pid)
{
    if (!initialized ||
        pid >= PROCESS_MAX)
        return 0;

    if (processes[pid].state == PROCESS_UNUSED ||
        processes[pid].state == PROCESS_TERMINATED)
        return 0;

    current_pid = pid;
    processes[pid].state = PROCESS_RUNNING;

    for (uint32_t i = 1;
         i < PROCESS_MAX;
         i++)
    {
        if (i != pid &&
            processes[i].state == PROCESS_RUNNING)
            processes[i].state = PROCESS_READY;
    }

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

    processes[current_pid].state =
        PROCESS_SLEEPING;

    /*
     * Vector 32 is the kernel's timer/scheduler
     * interrupt. The scheduler will save this
     * process's stack and select another process.
     */
    __asm__ volatile ("int $32");
}

uint32_t process_schedule(uint32_t current_stack)
{
    if (!initialized ||
        current_pid >= PROCESS_MAX)
        return current_stack;

    processes[current_pid].stack_pointer =
        current_stack;

    if (processes[current_pid].state ==
        PROCESS_RUNNING)
    {
        processes[current_pid].state =
            PROCESS_READY;
    }

    for (uint32_t offset = 1;
         offset < PROCESS_MAX;
         offset++)
    {
        uint32_t pid =
            (current_pid + offset) % PROCESS_MAX;

        if (processes[pid].state != PROCESS_READY)
            continue;

        current_pid = pid;
        processes[pid].state =
            PROCESS_RUNNING;

        return processes[pid].stack_pointer;
    }

    /*
     * Nothing else is ready. Keep the current
     * process running if it did not block.
     */
    if (processes[current_pid].state ==
        PROCESS_READY)
    {
        processes[current_pid].state =
            PROCESS_RUNNING;

        return processes[current_pid].stack_pointer;
    }

    /*
     * A blocked process with no runnable peer
     * cannot make progress. This should not happen
     * while the desktop process is alive.
     */
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
