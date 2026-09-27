#include "desktop_process.h"

#include "desktop.h"
#include "process.h"

static uint32_t desktop_pid;

void desktop_process_main(void)
{
    while (1)
    {
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

    /*
     * Desktop is a normal kernel thread. It gets its own scheduler-owned
     * stack and starts through the same synthetic IRQ frame used by every
     * other kernel process.
     */
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

