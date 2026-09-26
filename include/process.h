#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

#define PROCESS_MAX 32
#define PROCESS_NAME_MAX 31

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
} process_t;

void process_initialize(void);

int process_is_initialized(void);

int process_create(
    const char* name,
    uint32_t parent_pid
);

int process_set_running(uint32_t pid);

const process_t* process_get(uint32_t pid);

unsigned int process_count(void);

uint32_t process_current_pid(void);

#endif
