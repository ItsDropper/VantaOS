#include "os.h"

#include "desktop.h"
#include "desktop_process.h"
#include "filesystem.h"
#include "graphics.h"
#include "keyboard.h"
#include "mouse.h"
#include "process.h"
#include "serial.h"
#include "scheduler.h"
#include "shell.h"
#include "terminal.h"
#include "terminal_process.h"
#include "vfs.h"

extern void shell_set_multiboot_info(multiboot_info_t* mbd);

static unsigned long long boot_terminal;
static unsigned long long boot_keyboard;
static unsigned long long boot_mouse;
static unsigned long long boot_shell;

#ifdef VANTAOS_SELF_TEST
static uint32_t scheduler_probe_pid;

static void scheduler_probe_entry(void)
{
    serial_write("VANTAOS_SCHEDULER_PROBE_OK\n");
}

static void scheduler_reap_observer_entry(void)
{
    for (uint32_t attempt = 0; attempt < 1000U; attempt++)
    {
        if (process_get(scheduler_probe_pid) == NULL)
        {
            serial_write("VANTAOS_SCHEDULER_REAP_OK\n");
            return;
        }

        __asm__ volatile ("hlt");
    }

    serial_write("VANTAOS_SCHEDULER_REAP_FAIL\n");
}

static void scheduler_probe_initialize(void)
{
    int probe_pid = process_create_kernel(
        "sched-probe",
        process_current_pid(),
        scheduler_probe_entry
    );

    if (probe_pid < 0)
    {
        serial_write("VANTAOS_SCHEDULER_PROBE_CREATE_FAIL\n");
        return;
    }

    scheduler_probe_pid = (uint32_t)probe_pid;

    if (process_create_kernel(
            "sched-reap-check",
            process_current_pid(),
            scheduler_reap_observer_entry
        ) < 0)
    {
        serial_write("VANTAOS_SCHEDULER_OBSERVER_CREATE_FAIL\n");
    }
}
#endif

static inline unsigned long long read_tsc(void)
{
    unsigned int low;
    unsigned int high;

    __asm__ volatile (
        "rdtsc"
        : "=a"(low), "=d"(high)
    );

    return ((unsigned long long)high << 32) | low;
}

void os_initialize(multiboot_info_t* mbd)
{
    terminal_initialize();
    boot_terminal = read_tsc();

    keyboard_initialize();
    boot_keyboard = read_tsc();

    int graphics_ready =
        graphics_initialize(mbd);

    terminal_write("Graphics diagnostics:\n");
    terminal_write("  Multiboot flags: ");
    terminal_write_hex(mbd ? mbd->flags : 0);
    terminal_write("\n");

    terminal_write("  Framebuffer: ");
    terminal_write_hex(
        mbd ? (uint32_t)mbd->framebuffer_addr : 0
    );
    terminal_write("\n");

    terminal_write("  Type: ");
    terminal_write_hex(
        mbd ? mbd->framebuffer_type : 0
    );
    terminal_write("\n");

    terminal_write("  BPP: ");
    terminal_write_hex(
        mbd ? mbd->framebuffer_bpp : 0
    );
    terminal_write("\n");

    terminal_write("  Graphics init: ");
    terminal_write(
        graphics_ready ? "YES\n" : "NO\n"
    );

    filesystem_initialize(mbd);
    vfs_initialize();

    process_initialize();

    /*
     * Register the boot/desktop execution context as PID 1 before any
     * application processes are created. Its stack is the real kernel
     * boot stack, and the first timer IRQ will replace its saved ESP
     * with a genuine PUSHA/IRET frame.
     */
    process_attach_current("desktop", 0);

    scheduler_initialize();
    terminal_process_initialize();
    desktop_process_initialize();

    mouse_initialize();
    boot_mouse = read_tsc();

    shell_initialize();
    shell_set_multiboot_info(mbd);
    boot_shell = read_tsc();

    desktop_initialize(mbd);

#ifdef VANTAOS_SELF_TEST
    scheduler_probe_initialize();
#endif
}

void os_run(void)
{
    terminal_write("\nKernel initialized successfully.\n");

    terminal_write(
        filesystem_is_initialized() ?
        "Filesystem: online.\n" :
        "Filesystem: offline.\n"
    );

    terminal_write(
        process_is_initialized() ?
        "Process manager: online.\n" :
        "Process manager: offline.\n"
    );

    terminal_write(
        "Paging: enabled (identity-mapped 16 MiB).\n"
    );

    terminal_write(
        "Type 'help' in the Terminal app.\n\n"
    );

    desktop_update();
    desktop_present();

    __asm__ volatile ("sti");

    serial_write("VANTAOS_BOOT_OK\n");
    desktop_process_main();
}

unsigned long long kernel_boot_terminal(void)
{
    return boot_terminal;
}

unsigned long long kernel_boot_keyboard(void)
{
    return boot_keyboard;
}

unsigned long long kernel_boot_mouse(void)
{
    return boot_mouse;
}

unsigned long long kernel_boot_shell(void)
{
    return boot_shell;
}
