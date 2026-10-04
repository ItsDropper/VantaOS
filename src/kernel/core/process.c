#include "process.h"

#include <stddef.h>
#include <stdint.h>

extern unsigned char stack_bottom;
extern unsigned char stack_top;
extern unsigned char _start;
extern unsigned char _end;
extern unsigned char _kernel_text_start;
extern unsigned char _kernel_text_end;
extern void process_entry_trampoline(void);

static process_t processes[PROCESS_MAX];
static uint8_t process_stacks[PROCESS_MAX][PROCESS_STACK_SIZE]
    __attribute__((aligned(16)));

static int initialized;
static uint32_t current_pid;

static void process_copy_name(char* destination, const char* source)
{
    unsigned int i = 0;
    while (source && source[i] && i < PROCESS_NAME_MAX)
        destination[i++] = source[i];
    destination[i] = 0;
}

static void process_reset_record(process_t* process)
{
    process->parent_pid = 0;
    process->state = PROCESS_UNUSED;
    process->name[0] = 0;
    process->stack_pointer = 0;
    process->entry = NULL;
    process->kernel_stack = NULL;
    process->address_space = NULL;

    process->context.r15 = 0;
    process->context.r14 = 0;
    process->context.r13 = 0;
    process->context.r12 = 0;
    process->context.r11 = 0;
    process->context.r10 = 0;
    process->context.r9 = 0;
    process->context.r8 = 0;
    process->context.rsi = 0;
    process->context.rdi = 0;
    process->context.rbp = 0;
    process->context.rdx = 0;
    process->context.rcx = 0;
    process->context.rbx = 0;
    process->context.rax = 0;
    process->context.rsp = 0;
    process->context.rip = 0;
    process->context.rflags = 0x202U;
    process->context.cs = 0x08U;
    process->context.ss = 0x10U;
}

static int process_allocate_common(process_t* process,
                                    const char* name,
                                    uint32_t parent_pid)
{
    if (!process || !name || !name[0] ||
        !paging_get_kernel_address_space())
        return 0;

    process_reset_record(process);
    process->parent_pid = parent_pid;
    process->state = PROCESS_READY;
    process_copy_name(process->name, name);
    process->address_space = paging_get_kernel_address_space();

    return process->address_space != NULL;
}

void process_entry_dispatch(void)
{
    uint32_t pid = process_current_pid();
    const process_t* process = process_get(pid);

    if (process && process->entry)
        process->entry();

    process_terminate_current();
}

static void process_prepare_stack(process_t* process, uintptr_t stack_top)
{
    uint64_t* stack = (uint64_t*)stack_top;

    /*
     * x86_64 IRETQ always consumes the complete long-mode interrupt
     * frame: RIP, CS, RFLAGS, RSP, SS. Build that frame below the
     * 15 saved GPRs so RESTORE_ALL + IRETQ can enter the process safely.
     */
    *(--stack) = 0x10U; /* SS */
    *(--stack) = (uint64_t)stack_top; /* RSP after IRETQ */
    *(--stack) = 0x202U; /* RFLAGS: IF enabled */
    *(--stack) = 0x08U; /* CS */
    *(--stack) = (uint64_t)(uintptr_t)process_entry_trampoline; /* RIP */

    for (int i = 0; i < 15; i++)
        *(--stack) = 0;

    process->stack_pointer = (uint64_t)(uintptr_t)stack;
    process->context.rsp = process->stack_pointer;
    process->context.rip = (uint64_t)(uintptr_t)process_entry_trampoline;
    process->context.rflags = 0x202U;
    process->context.cs = 0x08U;
    process->context.ss = 0x10U;
}

void process_initialize(void)
{
    initialized = 0;
    current_pid = 0;

    for (uint32_t i = 0; i < PROCESS_MAX; i++)
    {
        processes[i].pid = i;
        process_reset_record(&processes[i]);
    }

    initialized = 1;
}

int process_is_initialized(void)
{
    return initialized != 0;
}

int process_attach_current(const char* name, uint32_t parent_pid)
{
    if (!initialized || !name || !name[0])
        return -1;

    for (uint32_t pid = 1; pid < PROCESS_MAX; pid++)
    {
        if (processes[pid].state != PROCESS_UNUSED &&
            processes[pid].state != PROCESS_TERMINATED)
            continue;

        process_t* process = &processes[pid];

        if (!process_allocate_common(process, name, parent_pid))
            return -1;

        uint64_t current_stack;
        __asm__ volatile ("mov %%rsp, %0" : "=r"(current_stack));

        process->state = PROCESS_RUNNING;
        process->stack_pointer = current_stack;
        process->context.rsp = current_stack;
        process->context.rip =
            (uint64_t)(uintptr_t)__builtin_return_address(0);
        process->context.cs = 0x08U;
        process->context.ss = 0x10U;
        process->kernel_stack = (void*)(uintptr_t)&stack_bottom;

        current_pid = pid;
        return (int)pid;
    }

    return -1;
}

int process_create_kernel(const char* name, uint32_t parent_pid,
                           process_entry_t entry)
{
    if (!initialized || !name || !name[0] || !entry)
        return -1;

    for (uint32_t pid = 1; pid < PROCESS_MAX; pid++)
    {
        if (processes[pid].state != PROCESS_UNUSED &&
            processes[pid].state != PROCESS_TERMINATED)
            continue;

        process_t* process = &processes[pid];

        if (!process_allocate_common(process, name, parent_pid))
            return -1;

        process->kernel_stack = process_stacks[pid];
        process->entry = entry;

        uintptr_t stack_top =
            (uintptr_t)process->kernel_stack + PROCESS_STACK_SIZE;
        stack_top &= ~(uintptr_t)0x0FU;

        process_prepare_stack(process, stack_top);
        return (int)pid;
    }

    return -1;
}

int process_mark_running(uint32_t pid)
{
    if (!initialized || pid == 0 || pid >= PROCESS_MAX ||
        processes[pid].state == PROCESS_UNUSED ||
        processes[pid].state == PROCESS_TERMINATED)
        return 0;

    processes[pid].state = PROCESS_RUNNING;
    return 1;
}

int process_mark_ready(uint32_t pid)
{
    if (!initialized || pid == 0 || pid >= PROCESS_MAX ||
        processes[pid].state == PROCESS_UNUSED ||
        processes[pid].state == PROCESS_TERMINATED)
        return 0;

    processes[pid].state = PROCESS_READY;
    return 1;
}

int process_set_running(uint32_t pid)
{
    if (!initialized || pid >= PROCESS_MAX ||
        processes[pid].state == PROCESS_UNUSED ||
        processes[pid].state == PROCESS_TERMINATED)
        return 0;

    if (processes[current_pid].state == PROCESS_RUNNING &&
        current_pid != pid)
        processes[current_pid].state = PROCESS_READY;

    current_pid = pid;
    processes[pid].state = PROCESS_RUNNING;
    return 1;
}

int process_switch_to(uint32_t pid)
{
    if (!initialized || pid == 0 || pid >= PROCESS_MAX)
        return 0;

    if (processes[pid].state != PROCESS_READY &&
        processes[pid].state != PROCESS_RUNNING)
        return 0;

    if (current_pid != pid &&
        current_pid < PROCESS_MAX &&
        processes[current_pid].state == PROCESS_RUNNING)
        processes[current_pid].state = PROCESS_READY;

    current_pid = pid;
    processes[pid].state = PROCESS_RUNNING;
    return 1;
}

int process_wake(uint32_t pid)
{
    if (!initialized || pid >= PROCESS_MAX ||
        processes[pid].state != PROCESS_SLEEPING)
        return 0;

    processes[pid].state = PROCESS_READY;
    return 1;
}

void process_block_current(void)
{
    if (initialized && current_pid < PROCESS_MAX)
        processes[current_pid].state = PROCESS_SLEEPING;
}

int process_terminate(uint32_t pid)
{
    if (!initialized || pid == 0 || pid >= PROCESS_MAX)
        return 0;

    process_t* process = &processes[pid];
    if (process->state == PROCESS_UNUSED ||
        process->state == PROCESS_TERMINATED)
        return 0;

    process->state = PROCESS_TERMINATED;

    if (pid == current_pid)
        return 1;

    return process_reap(pid);
}

void process_terminate_current(void)
{
    if (!initialized || current_pid == 0 ||
        current_pid >= PROCESS_MAX)
        return;

    if (processes[current_pid].state != PROCESS_UNUSED)
        processes[current_pid].state = PROCESS_TERMINATED;
}

void process_save_stack(uint32_t pid, uint64_t stack_pointer)
{
    if (!initialized || pid >= PROCESS_MAX ||
        processes[pid].state == PROCESS_UNUSED)
        return;

    processes[pid].stack_pointer = stack_pointer;
    processes[pid].context.rsp = stack_pointer;
}

uint64_t process_get_stack(uint32_t pid)
{
    if (!initialized || pid >= PROCESS_MAX ||
        processes[pid].state == PROCESS_UNUSED)
        return 0;

    return processes[pid].stack_pointer;
}

int process_stack_is_valid(uint32_t pid)
{
    if (!initialized || pid == 0 || pid >= PROCESS_MAX)
        return 0;

    process_t* process = &processes[pid];

    if (process->state == PROCESS_UNUSED ||
        process->stack_pointer == 0 ||
        process->kernel_stack == NULL)
        return 0;

    uintptr_t stack_base = (uintptr_t)process->kernel_stack;
    uintptr_t stack_end =
        process->entry != NULL ?
        stack_base + PROCESS_STACK_SIZE :
        (uintptr_t)&stack_top;
    uintptr_t frame = (uintptr_t)process->stack_pointer;

    if (frame < stack_base || frame + 160U > stack_end)
        return 0;

    uint64_t* values = (uint64_t*)frame;

    if (values[16] != 0x08U ||
        (values[17] & 0x00000200U) == 0 ||
        values[19] != 0x10U)
        return 0;

    uintptr_t rip = (uintptr_t)values[15];
    uintptr_t code_start = (uintptr_t)&_kernel_text_start;
    uintptr_t code_end = (uintptr_t)&_kernel_text_end;

    return rip >= code_start && rip < code_end;
}

int process_reap(uint32_t pid)
{
    if (!initialized || pid == 0 || pid >= PROCESS_MAX ||
        pid == current_pid)
        return 0;

    process_t* process = &processes[pid];
    if (process->state != PROCESS_TERMINATED)
        return 0;

    process->address_space = NULL;
    process->kernel_stack = NULL;
    process_reset_record(process);
    process->pid = pid;
    return 1;
}

uint64_t process_schedule(uint64_t current_stack)
{
    if (!initialized || current_pid >= PROCESS_MAX)
        return current_stack;

    process_save_stack(current_pid, current_stack);
    return current_stack;
}

const process_t* process_get(uint32_t pid)
{
    if (!initialized || pid >= PROCESS_MAX ||
        processes[pid].state == PROCESS_UNUSED)
        return NULL;

    return &processes[pid];
}

unsigned int process_count(void)
{
    if (!initialized)
        return 0;

    unsigned int count = 0;
    for (uint32_t i = 0; i < PROCESS_MAX; i++)
        if (processes[i].state != PROCESS_UNUSED &&
            processes[i].state != PROCESS_TERMINATED)
            count++;

    return count;
}

uint32_t process_current_pid(void)
{
    return current_pid;
}
