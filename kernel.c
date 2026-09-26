#include "gdt.h"
#include "graphics.h"
#include "heap.h"
#include "interrupts.h"
#include "keyboard.h"
#include "multiboot.h"
#include "paging.h"
#include "pmm.h"
#include "shell.h"
#include "terminal.h"
#include "mouse.h"

extern void shell_set_multiboot_info(multiboot_info_t* mbd);

static inline unsigned long long read_tsc(void)
{
    unsigned int low;
    unsigned int high;

    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));

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

    int graphics_ready =
        graphics_initialize(mbd);

    heap_initialize();
    boot_memory = read_tsc();
    boot_interrupts = boot_memory;

    mouse_initialize();
    boot_mouse = read_tsc();

    shell_initialize();
    shell_set_multiboot_info(mbd);
    boot_shell = read_tsc();

    __asm__ volatile ("sti");

    terminal_write("\nKernel initialized successfully.\n");
    terminal_write("Paging: enabled (identity-mapped 16 MiB).\n");
    terminal_write("Type 'help' for available commands.\n\n");

    if (graphics_ready)
        graphics_present();

    shell_show_prompt();

    while (1)
    {
        if (mouse_has_event())
        {
            if (mouse_has_wheel_event())
            {
                int wheel_delta = mouse_get_wheel_delta();

                if (wheel_delta > 0)
                    terminal_scroll_down();

                if (wheel_delta < 0)
                    terminal_scroll_up();
            }

            if (graphics_ready)
                graphics_present();

            continue;
        }

        if (keyboard_has_event())
        {
            keyboard_event_t event = keyboard_get_event();

        if (keyboard_has_event())
        {
            keyboard_event_t event = keyboard_get_event();

            if (event == KEY_EVENT_PAGE_UP)
                terminal_scroll_up();
            else if (event == KEY_EVENT_PAGE_DOWN)
                terminal_scroll_down();
            else
                shell_handle_event(event);

            continue;
        }

        char c = keyboard_get_char();

        if (c == 0)
        {
            __asm__ volatile ("hlt");
            continue;
        }

        shell_handle_char(c);
    }
}

unsigned long long kernel_boot_start(void) { return boot_start; }
unsigned long long kernel_boot_gdt(void) { return boot_gdt; }
unsigned long long kernel_boot_terminal(void) { return boot_terminal; }
unsigned long long kernel_boot_keyboard(void) { return boot_keyboard; }
unsigned long long kernel_boot_mouse(void) { return boot_mouse; }
unsigned long long kernel_boot_interrupts(void) { return boot_interrupts; }
unsigned long long kernel_boot_shell(void) { return boot_shell; }
