#include "terminal_process.h"
#include "process.h"
#include "keyboard.h"
#include "shell.h"
#include "terminal.h"

static uint32_t terminal_pid;
static volatile int exit_requested;
static volatile int redraw_requested;

static void terminal_prepare_session(void)
{
    terminal_reset();
    shell_initialize();

    terminal_write("\nVantaOS Terminal\n");
    shell_show_prompt();
}

static void terminal_process_main(void)
{
    while (!exit_requested)
    {
        int changed = 0;

        if (keyboard_has_event())
        {
            keyboard_event_t event =
                keyboard_get_event();

            if (event == KEY_EVENT_PAGE_UP)
                terminal_scroll_up();
            else if (event == KEY_EVENT_PAGE_DOWN)
                terminal_scroll_down();
            else
                shell_handle_event(event);

            changed = 1;
        }

        char character =
            keyboard_get_char();

        if (character != 0)
        {
            if (character == 27)
            {
                exit_requested = 1;
                break;
            }

            shell_handle_char(character);
            changed = 1;
        }

        if (changed)
            redraw_requested = 1;

        __asm__ volatile ("hlt");
    }

    process_terminate_current();

    while (1)
        __asm__ volatile ("hlt");
}

void terminal_process_initialize(void)
{
    terminal_pid = 0;
    exit_requested = 0;
    redraw_requested = 0;
}

int terminal_process_start(uint32_t parent_pid)
{
    if (terminal_pid != 0)
        return (int)terminal_pid;

    exit_requested = 0;
    redraw_requested = 1;
    terminal_prepare_session();

    int pid =
        process_create_kernel(
            "terminal",
            parent_pid,
            terminal_process_main
        );

    if (pid < 0)
        return -1;

    terminal_pid = (uint32_t)pid;
    return pid;
}

void terminal_process_request_exit(void)
{
    exit_requested = 1;
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
    if (terminal_pid == 0)
        return 0;

    const process_t* process =
        process_get(terminal_pid);

    if (!process)
    {
        terminal_pid = 0;
        return 0;
    }

    if (process->state == PROCESS_TERMINATED ||
        process->state == PROCESS_UNUSED)
    {
        terminal_pid = 0;
        return 0;
    }

    return 1;
}

uint32_t terminal_process_pid(void)
{
    return terminal_pid;
}
