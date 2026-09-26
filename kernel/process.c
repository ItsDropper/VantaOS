#include "process.h"

static process_t processes[PROCESS_MAX];
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
    }

    processes[1].pid = 1;
    processes[1].parent_pid = 0;
    processes[1].state = PROCESS_RUNNING;
    process_copy_name(
        processes[1].name,
        "terminal"
    );

    current_pid = 1;
    initialized = 1;
}

int process_is_initialized(void)
{
    return initialized;
}

int process_create(
    const char* name,
    uint32_t parent_pid
)
{
    if (!initialized || name == 0 || name[0] == 0)
        return -1;

    for (uint32_t pid = 2; pid < PROCESS_MAX; pid++)
    {
        if (processes[pid].state != PROCESS_UNUSED &&
            processes[pid].state != PROCESS_TERMINATED)
            continue;

        processes[pid].pid = pid;
        processes[pid].parent_pid = parent_pid;
        processes[pid].state = PROCESS_READY;

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
    if (!initialized || pid >= PROCESS_MAX)
        return 0;

    if (processes[pid].state == PROCESS_UNUSED ||
        processes[pid].state == PROCESS_TERMINATED)
        return 0;

    current_pid = pid;
    processes[pid].state = PROCESS_RUNNING;

    for (uint32_t i = 1; i < PROCESS_MAX; i++)
    {
        if (i != pid &&
            processes[i].state == PROCESS_RUNNING)
            processes[i].state = PROCESS_READY;
    }

    return 1;
}

const process_t* process_get(uint32_t pid)
{
    if (!initialized || pid >= PROCESS_MAX)
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

    for (unsigned int i = 0; i < PROCESS_MAX; i++)
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
