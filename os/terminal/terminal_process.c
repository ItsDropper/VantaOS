#include "terminal_process.h"
#include "process.h"
#include "terminal.h"

static uint32_t terminal_pid;
static volatile int redraw_requested;
static volatile int input_enabled;

void terminal_process_initialize(void)
{
    terminal_pid = 0U;
    __atomic_store_n(&redraw_requested, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&input_enabled, 0, __ATOMIC_RELEASE);
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
        __atomic_store_n(&redraw_requested, 1, __ATOMIC_RELEASE);
        return -1;
    }

    terminal_pid = (uint32_t)pid;
    __atomic_store_n(&redraw_requested, 1, __ATOMIC_RELEASE);
    return pid;
}

void terminal_process_request_exit(void)
{
    uint32_t pid = terminal_pid;
    terminal_pid = 0U;
    __atomic_store_n(&input_enabled, 0, __ATOMIC_RELEASE);
    if (pid != 0U)
        (void)process_terminate(pid);
}

void terminal_process_set_input_enabled(int enabled)
{
    __atomic_store_n(&input_enabled, enabled != 0, __ATOMIC_RELEASE);
}

int terminal_process_input_enabled(void)
{
    return __atomic_load_n(&input_enabled, __ATOMIC_ACQUIRE) != 0;
}

void terminal_process_mark_redraw(void)
{
    __atomic_store_n(&redraw_requested, 1, __ATOMIC_RELEASE);
}

int terminal_process_consume_redraw(void)
{
    return __atomic_exchange_n(&redraw_requested, 0, __ATOMIC_ACQ_REL) != 0;
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
