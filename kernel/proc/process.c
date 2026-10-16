#include "process.h"
#include "cpu_local.h"
#include "apic.h"
#include "spinlock.h"

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
static uint32_t legacy_current_pid;

static spinlock_t process_table_lock = SPINLOCK_INITIALIZER;

static uint64_t process_table_lock_acquire(void)
{
    return spinlock_lock_irqsave(&process_table_lock);
}

static void process_table_lock_release(uint64_t flags)
{
    spinlock_unlock_irqrestore(&process_table_lock, flags);
}

static cpu_local_t* process_local_state(void)
{
    cpu_local_t* local = cpu_local_current();
    if (!local || local->self != local || local->online == 0U)
        return NULL;
    return local;
}

uint32_t process_current_cpu_id(void)
{
    cpu_local_t* local = process_local_state();
    return local ? local->logical_id : 0U;
}

static uint32_t process_current_slot(void)
{
    cpu_local_t* local = process_local_state();
    return local ? local->current_pid : legacy_current_pid;
}

static void process_set_current_slot(uint32_t pid)
{
    cpu_local_t* local = process_local_state();
    if (local)
        local->current_pid = pid;
    else
        legacy_current_pid = pid;
}

static void process_copy_name(char* destination, const char* source)
{
    unsigned int i = 0;
    while (source && i < PROCESS_NAME_MAX && source[i])
    {
        destination[i] = source[i];
        i++;
    }
    destination[i] = 0;
}

static void process_reset_record(process_t* process)
{
    process->parent_pid = 0;
    process->owner_cpu = PROCESS_CPU_UNOWNED;
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
    process->affinity_cpu = process_current_cpu_id();
    process_copy_name(process->name, name);
    process->address_space = paging_get_kernel_address_space();

    return process->address_space != NULL;
}

void process_entry_dispatch(void)
{
    uint32_t pid = process_current_pid();
    process_t process;
    if (process_snapshot(pid, &process) && process.entry)
        process.entry();

    process_terminate_current();
}

static void process_prepare_stack(process_t* process, uintptr_t stack_top)
{
    uint64_t* stack = (uint64_t*)stack_top;

    /*
     * Initial context layout consumed by RESTORE_ALL followed by IRETQ.
     * This ring-0 IRETQ consumes RIP, CS, and RFLAGS. RSP and SS are
     * reserved words at the top of this synthetic initial stack.
     *
     * Offsets: padding 0; GPRs +8..+120; RIP +128; CS +136;
     * RFLAGS +144; reserved RSP +152; reserved SS +160.
     */
    *(--stack) = 0x10U; /* SS */
    *(--stack) = (uint64_t)stack_top; /* RSP after IRETQ */
    *(--stack) = 0x202U; /* RFLAGS: IF enabled */
    *(--stack) = 0x08U; /* CS */
    *(--stack) = (uint64_t)(uintptr_t)process_entry_trampoline; /* RIP */

    for (int i = 0; i < 15; i++)
        *(--stack) = 0;

    *(--stack) = 0; /* IRQ C-call alignment padding */

    process->stack_pointer = (uint64_t)(uintptr_t)stack;
    process->context.rsp = process->stack_pointer;
    process->context.rip = (uint64_t)(uintptr_t)process_entry_trampoline;
    process->context.rflags = 0x202U;
    process->context.cs = 0x08U;
    process->context.ss = 0x10U;
}

void process_initialize(void)
{
    __atomic_store_n(&initialized, 0, __ATOMIC_RELAXED);
    legacy_current_pid = 0;
    cpu_local_t* local = process_local_state();
    if (local)
        local->current_pid = 0;

    for (uint32_t i = 0; i < PROCESS_MAX; i++)
    {
        processes[i].pid = i;
        process_reset_record(&processes[i]);
    }

    __atomic_store_n(&initialized, 1, __ATOMIC_RELEASE);
}

int process_is_initialized(void)
{
    return __atomic_load_n(&initialized, __ATOMIC_ACQUIRE) != 0;
}

int process_attach_current(const char* name, uint32_t parent_pid)
{
    if (!initialized || !name || !name[0])
        return -1;

    uint64_t flags = process_table_lock_acquire();
    for (uint32_t pid = 1; pid < PROCESS_MAX; pid++)
    {
        if (processes[pid].state != PROCESS_UNUSED &&
            (processes[pid].state != PROCESS_TERMINATED ||
             processes[pid].owner_cpu != PROCESS_CPU_UNOWNED))
            continue;

        process_t* process = &processes[pid];
        if (!process_allocate_common(process, name, parent_pid))
        {
            process_table_lock_release(flags);
            return -1;
        }

        uint64_t current_stack;
        __asm__ volatile ("mov %%rsp, %0" : "=r"(current_stack));
        process->state = PROCESS_RUNNING;
        process->owner_cpu = process_current_cpu_id();
        process->stack_pointer = current_stack;
        process->context.rsp = current_stack;
        process->context.rip =
            (uint64_t)(uintptr_t)__builtin_return_address(0);
        process->context.cs = 0x08U;
        process->context.ss = 0x10U;
        process->kernel_stack = (void*)(uintptr_t)&stack_bottom;
        process_set_current_slot(pid);
        process_table_lock_release(flags);
        return (int)pid;
    }

    process_table_lock_release(flags);
    return -1;
}

static int process_create_kernel_affinity(const char* name,
                                          uint32_t parent_pid,
                                          process_entry_t entry,
                                          uint32_t affinity_cpu)
{
    if (!initialized || !name || !name[0] || !entry)
        return -1;

    uint64_t flags = process_table_lock_acquire();
    for (uint32_t pid = 1; pid < PROCESS_MAX; pid++)
    {
        if (processes[pid].state != PROCESS_UNUSED &&
            processes[pid].state != PROCESS_TERMINATED)
            continue;

        process_t* process = &processes[pid];
        if (!process_allocate_common(process, name, parent_pid))
        {
            process_table_lock_release(flags);
            return -1;
        }

        process->affinity_cpu = affinity_cpu;
        process->kernel_stack = process_stacks[pid];
        process->entry = entry;
        uintptr_t stack_top =
            (uintptr_t)process->kernel_stack + PROCESS_STACK_SIZE;
        stack_top &= ~(uintptr_t)0x0FU;
        process_prepare_stack(process, stack_top);
        process_table_lock_release(flags);
        return (int)pid;
    }

    process_table_lock_release(flags);
    return -1;
}


int process_create_kernel(const char* name, uint32_t parent_pid,
                          process_entry_t entry)
{
    return process_create_kernel_affinity(name, parent_pid, entry,
                                          process_current_cpu_id());
}

int process_create_kernel_any(const char* name, uint32_t parent_pid,
                              process_entry_t entry)
{
    return process_create_kernel_affinity(name, parent_pid, entry,
                                          PROCESS_CPU_ANY);
}



int process_create_kernel_on_cpu(const char* name, uint32_t parent_pid,
                                  process_entry_t entry, uint32_t cpu_id)
{
    return process_create_kernel_affinity(name, parent_pid, entry, cpu_id);
}


int process_create_idle_for_cpu(uint32_t cpu_id, const char* name,
                                process_entry_t entry)
{
    if (!initialized || cpu_id >= CPU_LOCAL_MAX || !name || !name[0] ||
        !entry)
        return 0;

    cpu_local_t* local = (cpu_local_t*)cpu_local_get(cpu_id);
    if (!local || local->self != local || local->online == 0U)
        return 0;

    int pid = process_create_kernel(name, 0U, entry);
    if (pid < 0)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    process_t* process = &processes[(uint32_t)pid];
    if (process->state != PROCESS_READY ||
        process->owner_cpu != PROCESS_CPU_UNOWNED ||
        local->idle_pid != 0U)
    {
        process_table_lock_release(flags);
        (void)process_terminate((uint32_t)pid);
        return 0;
    }
    process->affinity_cpu = cpu_id;
    __atomic_store_n(&local->idle_pid, (uint32_t)pid, __ATOMIC_RELEASE);
    process_table_lock_release(flags);
    return pid;
}

int process_mark_running(uint32_t pid)
{
    if (!initialized || pid == 0 || pid >= PROCESS_MAX)
        return 0;
    uint64_t flags = process_table_lock_acquire();
    int ok = processes[pid].state != PROCESS_UNUSED &&
             processes[pid].state != PROCESS_TERMINATED;
    if (ok)
        processes[pid].state = PROCESS_RUNNING;
    process_table_lock_release(flags);
    return ok;
}

int process_mark_ready(uint32_t pid)
{
    if (!initialized || pid == 0 || pid >= PROCESS_MAX)
        return 0;
    uint64_t flags = process_table_lock_acquire();
    int ok = processes[pid].state != PROCESS_UNUSED &&
             processes[pid].state != PROCESS_TERMINATED;
    if (ok)
        processes[pid].state = PROCESS_READY;
    process_table_lock_release(flags);
    return ok;
}

int process_set_running(uint32_t pid)
{
    if (!initialized || pid >= PROCESS_MAX)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    process_t* target = &processes[pid];
    uint32_t cpu_id = process_current_cpu_id();
    if (target->state == PROCESS_UNUSED ||
        target->state == PROCESS_TERMINATED ||
        (target->owner_cpu != PROCESS_CPU_UNOWNED &&
         target->owner_cpu != cpu_id))
    {
        process_table_lock_release(flags);
        return 0;
    }

    uint32_t current_pid = process_current_slot();
    if (current_pid < PROCESS_MAX && current_pid != pid)
    {
        process_t* current = &processes[current_pid];
        if (current->state == PROCESS_RUNNING)
            current->state = PROCESS_READY;
        if (current->owner_cpu == cpu_id)
            current->owner_cpu = PROCESS_CPU_UNOWNED;
    }

    process_set_current_slot(pid);
    target->state = PROCESS_RUNNING;
    target->owner_cpu = cpu_id;
    process_table_lock_release(flags);
    return 1;
}

int process_claim_for_cpu(uint32_t next_pid, uint32_t current_pid,
                          uint32_t cpu_id)
{
    if (!initialized || next_pid == 0 || next_pid >= PROCESS_MAX ||
        current_pid >= PROCESS_MAX)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    process_t* next = &processes[next_pid];
    if (next->state != PROCESS_READY ||
        next->owner_cpu != PROCESS_CPU_UNOWNED ||
        (next->affinity_cpu != PROCESS_CPU_ANY &&
         next->affinity_cpu != cpu_id))
    {
        process_table_lock_release(flags);
        return 0;
    }

    if (current_pid != 0 && current_pid != next_pid)
    {
        process_t* current = &processes[current_pid];
        if (current->owner_cpu != cpu_id ||
            (current->state != PROCESS_RUNNING &&
             current->state != PROCESS_READY &&
             current->state != PROCESS_SLEEPING &&
             current->state != PROCESS_TERMINATED))
        {
            process_table_lock_release(flags);
            return 0;
        }

        if (current->state == PROCESS_RUNNING)
            current->state = PROCESS_READY;
        current->owner_cpu = PROCESS_CPU_UNOWNED;
    }

    next->state = PROCESS_RUNNING;
    next->owner_cpu = cpu_id;
    process_set_current_slot(next_pid);
    process_table_lock_release(flags);
    return 1;
}

int process_switch_to(uint32_t pid)
{
    return process_claim_for_cpu(pid, process_current_slot(),
                                 process_current_cpu_id());
}

static void process_send_wake_ipi(uint32_t owner_cpu,
                                       uint32_t affinity_cpu)
{
    if (!apic_is_enabled())
        return;

    if (owner_cpu != PROCESS_CPU_UNOWNED)
    {
        const cpu_local_t* local = cpu_local_get(owner_cpu);
        if (local)
            (void)apic_send_reschedule_ipi(local->apic_id);
        return;
    }

    if (affinity_cpu != PROCESS_CPU_ANY)
    {
        const cpu_local_t* local = cpu_local_get(affinity_cpu);
        if (local)
            (void)apic_send_reschedule_ipi(local->apic_id);
        return;
    }

    /* An unpinned task can be claimed by any online CPU. */
    for (uint32_t cpu = 0; cpu < cpu_local_capacity(); cpu++)
    {
        const cpu_local_t* local = cpu_local_get(cpu);
        if (local)
            (void)apic_send_reschedule_ipi(local->apic_id);
    }
}

int process_wake(uint32_t pid)
{
    if (!initialized || pid == 0U || pid >= PROCESS_MAX)
        return 0;

    uint32_t owner_cpu = PROCESS_CPU_UNOWNED;
    uint32_t affinity_cpu = PROCESS_CPU_ANY;
    uint64_t flags = process_table_lock_acquire();
    process_t* process = &processes[pid];
    int ok = process->state == PROCESS_SLEEPING;
    if (ok)
    {
        process->state = PROCESS_READY;
        owner_cpu = process->owner_cpu;
        affinity_cpu = process->affinity_cpu;
    }
    process_table_lock_release(flags);

    if (ok)
        process_send_wake_ipi(owner_cpu, affinity_cpu);
    return ok;
}


static void process_request_cpu_reschedule(uint32_t cpu_id)
{
    const cpu_local_t* local = cpu_local_get(cpu_id);
    if (local && apic_is_enabled())
        (void)apic_send_reschedule_ipi(local->apic_id);
}

int process_set_affinity(uint32_t pid, uint32_t cpu_id)
{
    if (!initialized || pid == 0U || pid >= PROCESS_MAX ||
        (cpu_id != PROCESS_CPU_ANY &&
         (cpu_id >= CPU_LOCAL_MAX || !cpu_local_get(cpu_id))))
        return 0;

    uint32_t old_affinity = PROCESS_CPU_ANY;
    uint32_t owner_cpu = PROCESS_CPU_UNOWNED;
    uint64_t flags = process_table_lock_acquire();
    process_t* process = &processes[pid];
    if (process->state == PROCESS_UNUSED ||
        process->state == PROCESS_TERMINATED)
    {
        process_table_lock_release(flags);
        return 0;
    }

    old_affinity = process->affinity_cpu;
    owner_cpu = process->owner_cpu;
    if (old_affinity == cpu_id)
    {
        process_table_lock_release(flags);
        return 1;
    }

    process->affinity_cpu = cpu_id;
    process_table_lock_release(flags);

    /*
     * First make the old owner relinquish its stack, then prompt the
     * destination (or all CPUs for an unpinned process) to claim READY work.
     */
    if (owner_cpu != PROCESS_CPU_UNOWNED)
        process_request_cpu_reschedule(owner_cpu);

    if (cpu_id != PROCESS_CPU_ANY)
    {
        if (cpu_id != owner_cpu)
            process_request_cpu_reschedule(cpu_id);
    }
    else
    {
        for (uint32_t cpu = 0; cpu < cpu_local_capacity(); cpu++)
        {
            if (cpu != owner_cpu)
                process_request_cpu_reschedule(cpu);
        }
    }
    return 1;
}

void process_block_current(void)
{
    if (!initialized)
        return;
    uint64_t flags = process_table_lock_acquire();
    uint32_t current_pid = process_current_slot();
    if (current_pid < PROCESS_MAX)
        processes[current_pid].state = PROCESS_SLEEPING;
    /* Ownership remains with this CPU until it actually switches stacks. */
    process_table_lock_release(flags);
}

int process_terminate(uint32_t pid)
{
    if (!initialized || pid == 0 || pid >= PROCESS_MAX)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    process_t* process = &processes[pid];
    if (process->state == PROCESS_UNUSED ||
        process->state == PROCESS_TERMINATED)
    {
        process_table_lock_release(flags);
        return 0;
    }

    process->state = PROCESS_TERMINATED;
    int is_current = pid == process_current_slot();
    process_table_lock_release(flags);

    if (is_current)
        return 1;
    return process_reap(pid);
}

void process_terminate_current(void)
{
    if (!initialized)
        return;
    uint64_t flags = process_table_lock_acquire();
    uint32_t current_pid = process_current_slot();
    if (current_pid != 0 && current_pid < PROCESS_MAX &&
        processes[current_pid].state != PROCESS_UNUSED)
        processes[current_pid].state = PROCESS_TERMINATED;
    process_table_lock_release(flags);
}

void process_save_stack(uint32_t pid, uint64_t stack_pointer)
{
    if (!initialized || pid >= PROCESS_MAX)
        return;
    uint64_t flags = process_table_lock_acquire();
    if (processes[pid].state != PROCESS_UNUSED)
    {
        processes[pid].stack_pointer = stack_pointer;
        processes[pid].context.rsp = stack_pointer;
    }
    process_table_lock_release(flags);
}

uint64_t process_get_stack(uint32_t pid)
{
    if (!initialized || pid >= PROCESS_MAX)
        return 0;
    uint64_t flags = process_table_lock_acquire();
    uint64_t stack = processes[pid].state == PROCESS_UNUSED ?
                     0 : processes[pid].stack_pointer;
    process_table_lock_release(flags);
    return stack;
}

int process_stack_is_valid(uint32_t pid)
{
    if (!initialized || pid == 0 || pid >= PROCESS_MAX)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    process_t* process = &processes[pid];
    if (process->state == PROCESS_UNUSED ||
        process->stack_pointer == 0 ||
        process->kernel_stack == NULL)
    {
        process_table_lock_release(flags);
        return 0;
    }

    uintptr_t stack_base = (uintptr_t)process->kernel_stack;
    uintptr_t stack_end = process->entry != NULL ?
        stack_base + PROCESS_STACK_SIZE : (uintptr_t)&stack_top;
    uintptr_t frame = (uintptr_t)process->stack_pointer;
    if (frame < stack_base || frame > stack_end ||
        stack_end - frame < 19U * sizeof(uint64_t))
    {
        process_table_lock_release(flags);
        return 0;
    }

    uint64_t* values = (uint64_t*)frame;
    if (values[17] != 0x08U ||
        (values[18] & 0x00000200U) == 0)
    {
        process_table_lock_release(flags);
        return 0;
    }

    uintptr_t rip = (uintptr_t)values[16];
    uintptr_t code_start = (uintptr_t)&_kernel_text_start;
    uintptr_t code_end = (uintptr_t)&_kernel_text_end;
    int valid = rip >= code_start && rip < code_end;
    process_table_lock_release(flags);
    return valid;
}

int process_reap(uint32_t pid)
{
    if (!initialized || pid == 0 || pid >= PROCESS_MAX)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    if (pid == process_current_slot() ||
        processes[pid].state != PROCESS_TERMINATED ||
        processes[pid].owner_cpu != PROCESS_CPU_UNOWNED)
    {
        process_table_lock_release(flags);
        return 0;
    }

    process_t* process = &processes[pid];
    process->address_space = NULL;
    process->kernel_stack = NULL;
    process_reset_record(process);
    process->pid = pid;
    process_table_lock_release(flags);
    return 1;
}

uint64_t process_schedule(uint64_t current_stack)
{
    uint32_t current_pid = process_current_slot();
    if (!initialized || current_pid >= PROCESS_MAX)
        return current_stack;

    process_save_stack(current_pid, current_stack);
    return current_stack;
}


int process_snapshot(uint32_t pid, process_t* snapshot)
{
    if (!initialized || pid >= PROCESS_MAX || !snapshot)
        return 0;

    uint64_t flags = process_table_lock_acquire();
    int ok = processes[pid].state != PROCESS_UNUSED;
    if (ok)
        *snapshot = processes[pid];
    process_table_lock_release(flags);
    return ok;
}

unsigned int process_count(void)
{
    if (!initialized)
        return 0;
    uint64_t flags = process_table_lock_acquire();
    unsigned int count = 0;
    for (uint32_t i = 0; i < PROCESS_MAX; i++)
        if (processes[i].state != PROCESS_UNUSED &&
            processes[i].state != PROCESS_TERMINATED)
            count++;
    process_table_lock_release(flags);
    return count;
}

uint32_t process_current_pid(void)
{
    return process_current_slot();
}
