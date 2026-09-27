#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

#include "paging.h"

#define PROCESS_MAX 32
#define PROCESS_NAME_MAX 31
#define PROCESS_STACK_SIZE 16384U

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
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t eip;
    uint32_t eflags;
    uint32_t cs;
    uint32_t ss;
} process_context_t;

typedef struct
{
    uint32_t pid;
    uint32_t parent_pid;
    process_state_t state;
    char name[PROCESS_NAME_MAX + 1];

    uint32_t stack_pointer;
    process_entry_t entry;
    void* kernel_stack;
    paging_address_space_t* address_space;
    process_context_t context;
} process_t;

void process_initialize(void);

int process_is_initialized(void);

int process_create_kernel(
    const char* name,
    uint32_t parent_pid,
    process_entry_t entry
);

int process_attach_current(
    const char* name,
    uint32_t parent_pid
);

int process_set_running(uint32_t pid);
int process_mark_running(uint32_t pid);
int process_mark_ready(uint32_t pid);

int process_wake(uint32_t pid);
int process_terminate(uint32_t pid);

void process_block_current(void);
void process_terminate_current(void);

void process_save_stack(uint32_t pid, uint32_t stack_pointer);
uint32_t process_get_stack(uint32_t pid);
int process_switch_to(uint32_t pid);
int process_reap(uint32_t pid);

uint32_t process_schedule(uint32_t current_stack);

const process_t* process_get(uint32_t pid);

unsigned int process_count(void);

uint32_t process_current_pid(void);

#endif
