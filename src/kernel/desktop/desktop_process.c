#include "desktop_process.h"

#include "desktop.h"
#include "process.h"
#include "mouse.h"
#include "terminal_process.h"

static uint32_t desktop_pid;

void desktop_process_main(void)
{
    while (1)
    {
        mouse_process_events();
        terminal_process_poll_input();
        desktop_update();
        __asm__ volatile ("hlt");
    }
}

void desktop_process_initialize(void)
{
    desktop_pid = 0;
}

int desktop_process_start(uint32_t parent_pid)
{
    if (desktop_pid != 0)
        return (int)desktop_pid;

    int pid =
        process_create_kernel(
            "desktop",
            parent_pid,
            desktop_process_main
        );

    if (pid < 0)
        return -1;

    desktop_pid = (uint32_t)pid;
    return pid;
}
