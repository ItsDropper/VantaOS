#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "paging.h"

#define PROCESS_MAX 128
#define PROCESS_NAME_MAX 31
#define PROCESS_STACK_SIZE 16384U
#define PROCESS_CPU_UNOWNED UINT32_MAX
#define PROCESS_CPU_ANY PROCESS_CPU_UNOWNED

typedef void (*process_entry_t)(void);

typedef enum
{
    PROCESS_UNUSED = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_SLEEPING,
    PROCESS_TERMINATED
} process_state_t;

typedef struct process_context
{
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rsi, rdi, rbp, rdx, rcx, rbx, rax;
    uint64_t rsp, rip, rflags, cs, ss;
} process_context_t;

typedef struct
{
    uint32_t pid;
    uint32_t parent_pid;
    /* PROCESS_CPU_UNOWNED unless this process is executing on a CPU. */
    uint32_t owner_cpu;
    uint32_t affinity_cpu;
    process_state_t state;
    char name[PROCESS_NAME_MAX + 1];
    uint64_t stack_pointer;
    process_entry_t entry;
    void* kernel_stack;
    paging_address_space_t* address_space;
    process_context_t context;
} process_t;

void process_initialize(void);
int process_is_initialized(void);
int process_create_kernel(const char* name, uint32_t parent_pid, process_entry_t entry);
int process_create_kernel_any(const char* name, uint32_t parent_pid,
                              process_entry_t entry);
int process_create_kernel_on_cpu(const char* name, uint32_t parent_pid,
                                  process_entry_t entry, uint32_t cpu_id);
int process_set_affinity(uint32_t pid, uint32_t cpu_id);
int process_create_idle_for_cpu(uint32_t cpu_id, const char* name,
                                process_entry_t entry);
int process_attach_current(const char* name, uint32_t parent_pid);
int process_set_running(uint32_t pid);
int process_mark_ready(uint32_t pid);
int process_wake(uint32_t pid);
int process_terminate(uint32_t pid);
void process_block_current(void);
void process_terminate_current(void);
void process_save_stack(uint32_t pid, uint64_t stack_pointer);
uint64_t process_get_stack(uint32_t pid);
int process_stack_is_valid(uint32_t pid);
int process_switch_to(uint32_t pid);
/* Atomically releases current_pid and claims a READY process for cpu_id. */
int process_claim_for_cpu(uint32_t next_pid, uint32_t current_pid,
                          uint32_t cpu_id);
/* Claims a process and returns its validated context under the same lock. */
int process_claim_for_cpu_context(uint32_t next_pid, uint32_t current_pid,
                                  uint32_t cpu_id, uint64_t* stack_pointer_out,
                                  paging_address_space_t** address_space_out);
/* Scheduler transition APIs; finish must run after switching off the old stack. */
int process_prepare_context_switch(uint32_t next_pid, uint32_t current_pid,
                                   uint32_t cpu_id, uint64_t* next_stack_out,
                                   paging_address_space_t** next_address_space_out);
int process_finish_context_switch(uint32_t outgoing_pid, uint32_t cpu_id);
int process_reap(uint32_t pid);
uint64_t process_schedule(uint64_t current_stack);
/* Copies a process record while holding the process-table lock. */
int process_snapshot(uint32_t pid, process_t* snapshot);
/* Non-blocking snapshot for exception/fault paths that must not deadlock. */
int process_snapshot_try(uint32_t pid, process_t* snapshot);
unsigned int process_count(void);
uint32_t process_current_pid(void);
uint32_t process_current_cpu_id(void);

#endif
