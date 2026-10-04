#include "os.h"

#include "desktop.h"
#include "desktop_process.h"
#include "filesystem.h"
#include "graphics.h"
#include "heap.h"
#include "keyboard.h"
#include "mouse.h"
#include "process.h"
#include "scheduler.h"
#include "shell.h"
#include "terminal.h"
#include "terminal_process.h"
#include "vfs.h"

extern void shell_set_multiboot_info(multiboot_info_t* mbd);

static unsigned long long os_boot_terminal;
static unsigned long long os_boot_memory;
static unsigned long long os_boot_mouse;
static unsigned long long os_boot_shell;

extern unsigned long long kernel_boot_start(void);
extern unsigned long long kernel_boot_gdt(void);
extern unsigned long long kernel_boot_terminal(void);
extern unsigned long long kernel_boot_keyboard(void);
extern unsigned long long kernel_boot_mouse(void);
extern unsigned long long kernel_boot_interrupts(void);
extern unsigned long long kernel_boot_shell(void);

void os_initialize(multiboot_info_t* mbd)
{
    terminal_initialize();

    keyboard_initialize();

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

    heap_initialize();
    filesystem_initialize(mbd);
    vfs_initialize();

    process_initialize();
    scheduler_initialize();
    terminal_process_initialize();
    desktop_process_initialize();

    os_boot_memory = kernel_boot_start();

    mouse_initialize();
    os_boot_mouse = kernel_boot_start();

    shell_initialize();
    shell_set_multiboot_info(mbd);
    os_boot_shell = kernel_boot_start();

    desktop_initialize(mbd);
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

    desktop_process_main();
}
