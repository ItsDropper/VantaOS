#include "gdt.h"
#include "graphics.h"
#include "heap.h"
#include "interrupts.h"
#include "keyboard.h"
#include "multiboot.h"
#include "mouse.h"
#include "paging.h"
#include "pci.h"
#include "pmm.h"
#include "shell.h"
#include "terminal.h"
#include "filesystem.h"
#include "process.h"
#include "scheduler.h"
#include "vfs.h"
#include "terminal_process.h"
#include "desktop.h"

extern void shell_set_multiboot_info(multiboot_info_t* mbd);

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

static unsigned long long boot_start;
static unsigned long long boot_gdt;
static unsigned long long boot_terminal;
static unsigned long long boot_keyboard;
static unsigned long long boot_interrupts;
static unsigned long long boot_mouse;
static unsigned long long boot_memory;
static unsigned long long boot_shell;

void kernel_main(multiboot_info_t* mbd)
{
    boot_start = read_tsc();

    gdt_initialize();
    boot_gdt = read_tsc();

    terminal_initialize();
    boot_terminal = read_tsc();

    keyboard_initialize();
    boot_keyboard = read_tsc();

    pmm_initialize(mbd);
    interrupts_initialize();
    paging_initialize();
    pci_initialize();

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

    if (process_attach_current("desktop", 0) < 0)
        terminal_write("Desktop process initialization failed.\n");

    boot_memory = read_tsc();
    boot_interrupts = boot_memory;

    mouse_initialize();
    boot_mouse = read_tsc();

    shell_initialize();
    shell_set_multiboot_info(mbd);
    boot_shell = read_tsc();

    desktop_initialize(mbd);

    __asm__ volatile ("sti");

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

    desktop_present();

    while (1)
    {
        desktop_update();
        __asm__ volatile ("hlt");
    }
}

unsigned long long kernel_boot_start(void)
{
    return boot_start;
}

unsigned long long kernel_boot_gdt(void)
{
    return boot_gdt;
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

unsigned long long kernel_boot_interrupts(void)
{
    return boot_interrupts;
}

unsigned long long kernel_boot_shell(void)
{
    return boot_shell;
}
