#include "terminal_process.h"
#include "process.h"
#include "terminal.h"

static uint32_t terminal_pid;
static volatile int redraw_requested;

void terminal_process_initialize(void)
{
    terminal_pid = 0U;
    redraw_requested = 0;
}

int terminal_process_start(uint32_t parent_pid)
{
    if (terminal_pid != 0U)
    {
        if (terminal_process_is_running())
            return (int)terminal_pid;
        terminal_pid = 0U;
    }

    terminal_reset();
    const char* argv[1] = { "/TERMINAL.ELF" };
    int pid = process_spawn_user_elf_file(
        "/TERMINAL.ELF", parent_pid, 1U, argv);
    if (pid <= 0)
    {
        terminal_write("Terminal could not start /TERMINAL.ELF.\n");
        redraw_requested = 1;
        return -1;
    }

    terminal_pid = (uint32_t)pid;
    redraw_requested = 1;
    return pid;
}

void terminal_process_request_exit(void)
{
    uint32_t pid = terminal_pid;
    terminal_pid = 0U;
    if (pid != 0U)
        (void)process_terminate(pid);
}

void terminal_process_mark_redraw(void)
{
    redraw_requested = 1;
}

int terminal_process_consume_redraw(void)
{
    if (!redraw_requested)
        return 0;
    redraw_requested = 0;
    return 1;
}

int terminal_process_is_running(void)
{
    if (terminal_pid == 0U)
        return 0;

    process_t process;
    if (!process_snapshot(terminal_pid, &process) ||
        process.state == PROCESS_TERMINATED ||
        process.state == PROCESS_ZOMBIE ||
        process.state == PROCESS_UNUSED)
    {
        terminal_pid = 0U;
        return 0;
    }

    return 1;
}

uint32_t terminal_process_pid(void)
{
    return terminal_pid;
}
