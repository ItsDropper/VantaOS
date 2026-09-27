#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

#define PROCESS_MAX 32
#define PROCESS_NAME_MAX 31
#define PROCESS_STACK_SIZE 16384

typedef void (*process_entry_t)(void);

typedef enum
{
    PROCESS_UNUSED = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_SLEEPING,
    PROCESS_TERMINATED
} process_state_t;

typedef struct
{
    uint32_t pid;
    uint32_t parent_pid;
    process_state_t state;
    char name[PROCESS_NAME_MAX + 1];

    /*
     * The kernel stack is allocated now, but context switching is
     * intentionally still disabled.  stack_pointer is the value
     * a future assembly switch routine will load into ESP.
     */
    uint32_t stack_pointer;
    uintptr_t kernel_stack_base;
    uintptr_t kernel_stack_top;

    process_entry_t entry;
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

int process_wake(uint32_t pid);

void process_block_current(void);

uint32_t process_pick_next(void);

uint32_t process_schedule(uint32_t current_stack);

const process_t* process_get(uint32_t pid);

unsigned int process_count(void);

uint32_t process_current_pid(void);

#endif
