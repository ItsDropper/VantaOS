#include "file_explorer_process.h"

#include "file_explorer.h"
#include "graphics.h"
#include "process.h"

static uint32_t explorer_pid;

static void file_explorer_process_main(void)
{
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
        return (int)explorer_pid;

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
    if (graphics_get_active_panel() != 2)
        return;

    if (explorer_pid != 0)
        process_mark_running(explorer_pid);

    if (explorer_pid != 0)
        process_mark_ready(explorer_pid);
}

uint32_t file_explorer_process_pid(void)
{
    return explorer_pid;
}
