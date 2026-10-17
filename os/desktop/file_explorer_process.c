#include "file_explorer_process.h"

#include "graphics.h"
#include "process.h"
#include "serial.h"

static uint32_t explorer_pid;

void file_explorer_process_initialize(void)
{
    explorer_pid = 0U;
}

int file_explorer_process_start(uint32_t parent_pid)
{
    if (explorer_pid != 0U)
    {
        process_t process;
        if (process_snapshot(explorer_pid, &process) &&
            process.state != PROCESS_TERMINATED &&
            process.state != PROCESS_ZOMBIE &&
            process.state != PROCESS_REAPING &&
            process.state != PROCESS_UNUSED)
            return (int)explorer_pid;
        explorer_pid = 0U;
    }

    const char* argv[1] = { "/EXPLORER.ELF" };
    int pid = process_spawn_user_elf_file(
        "/EXPLORER.ELF", parent_pid, 1U, argv);
    if (pid <= 0)
    {
        serial_write("VANTAOS_EXPLORER_LAUNCH_FAIL\n");
        return -1;
    }

    explorer_pid = (uint32_t)pid;
    return pid;
}

void file_explorer_process_step(void)
{
    if (explorer_pid == 0U)
        return;

    process_t process;
    if (!process_snapshot(explorer_pid, &process) ||
        process.state == PROCESS_TERMINATED ||
        process.state == PROCESS_ZOMBIE ||
        process.state == PROCESS_REAPING ||
        process.state == PROCESS_UNUSED)
    {
        explorer_pid = 0U;
        if (graphics_get_active_panel() == 2)
            graphics_select_panel(0);
    }
}

uint32_t file_explorer_process_pid(void)
{
    return explorer_pid;
}
