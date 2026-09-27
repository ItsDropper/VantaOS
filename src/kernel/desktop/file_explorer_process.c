#include "file_explorer_process.h"

#include "file_explorer.h"
#include "graphics.h"
#include "process.h"

static uint32_t explorer_pid;

static void file_explorer_process_main(void)
{
    /*
     * Explorer is a persistent desktop application. Hiding its window
     * must never terminate or reap its process.
     */
    while (1)
        __asm__ volatile ("hlt");
}

void file_explorer_process_initialize(void)
{
    explorer_pid = 0;
}

int file_explorer_process_start(uint32_t parent_pid)
{
    if (explorer_pid != 0)
    {
        const process_t* process = process_get(explorer_pid);

        if (process &&
            process->state != PROCESS_TERMINATED &&
            process->state != PROCESS_UNUSED)
            return (int)explorer_pid;

        explorer_pid = 0;
    }

    int pid = process_create_kernel(
        "files",
        parent_pid,
        file_explorer_process_main
    );

    if (pid < 0)
        return -1;

    explorer_pid = (uint32_t)pid;
    return pid;
}

void file_explorer_process_step(void)
{
    /*
     * The process lifetime is independent from window visibility.
     * Minimize/close only changes active_panel; it must not make the
     * process disappear from the process table.
     */
    if (explorer_pid == 0)
        return;

    const process_t* process = process_get(explorer_pid);

    if (!process ||
        process->state == PROCESS_TERMINATED ||
        process->state == PROCESS_UNUSED)
    {
        explorer_pid = 0;
        return;
    }

    /*
     * Keep the persistent desktop application runnable whether its
     * window is visible or minimized.
     */
    process_mark_ready(explorer_pid);
}

uint32_t file_explorer_process_pid(void)
{
    return explorer_pid;
}
