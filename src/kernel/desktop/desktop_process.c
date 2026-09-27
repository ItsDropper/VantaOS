#include "desktop_process.h"

#include "desktop.h"
#include "process.h"

static uint32_t desktop_pid;

void desktop_process_initialize(void)
{
    desktop_pid = 0;
}

int desktop_process_start(uint32_t parent_pid)
{
    if (desktop_pid != 0)
        return (int)desktop_pid;

    /*
     * Desktop is the first interactive kernel task. Keep its initial
     * execution on the boot stack until the scheduler has a real saved
     * interrupt frame for it. This avoids fabricating an IRET frame for
     * the first desktop entry.
     */
    int pid =
        process_attach_current(
            "desktop",
            parent_pid
        );

    if (pid < 0)
        return -1;

    desktop_pid = (uint32_t)pid;
    return pid;
}

void desktop_process_run(void)
{
    if (desktop_pid == 0)
        return;

    while (1)
    {
        desktop_update();
        __asm__ volatile ("hlt");
    }
}
