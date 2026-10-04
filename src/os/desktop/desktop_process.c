#include "desktop_process.h"

#include "desktop.h"
#include "graphics.h"
#include "process.h"
#include "mouse.h"
#include "terminal_process.h"
#include "file_explorer_process.h"

static uint32_t desktop_pid;

void desktop_process_main(void)
{
    while (1)
    {
        mouse_process_events();
        if (graphics_get_active_panel() == 3)
            terminal_process_poll_input();
        desktop_update();
        file_explorer_process_step();
        __asm__ volatile ("hlt");
    }
}

void desktop_process_initialize(void)
{
    desktop_pid = 0;
    file_explorer_process_initialize();
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
