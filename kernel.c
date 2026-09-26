#include "gdt.h"
#include "graphics.h"
#include "heap.h"
#include "interrupts.h"
#include "keyboard.h"
#include "multiboot.h"
#include "paging.h"
#include "pmm.h"
#include "pci.h"
#include "shell.h"
#include "terminal.h"
#include "mouse.h"
#include "filesystem.h"
#include "process.h"

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

static int terminal_window_open = 0;
static int terminal_window_prompted = 0;
static int terminal_pid = -1;
static int graphics_ready_global = 0;

static void terminal_window_draw(void)
{
    if (!graphics_ready_global || !terminal_window_open)
        return;

    int width = (int)graphics_get_width();
    int height = (int)graphics_get_height();
    int window_w = 760;
    int window_h = 520;
    int window_x = width / 2 - window_w / 2;
    int window_y = 80;

    graphics_fill_rect(
        window_x + 8, window_y + 10,
        window_w, window_h, 0x00070A0F
    );

    graphics_fill_rect(
        window_x, window_y,
        window_w, window_h, 0x00101820
    );

    graphics_fill_rect(
        window_x, window_y,
        window_w, 44, 0x00212C3A
    );

    graphics_draw_text(
        window_x + 18, window_y + 14,
        "TERMINAL", 0x00FFFFFF, 2
    );

    graphics_fill_rect(
        window_x + window_w - 42,
        window_y + 10,
        28, 28, 0x00374452
    );

    graphics_draw_text(
        window_x + window_w - 34,
        window_y + 15,
        "X", 0x00FFFFFF, 2
    );

    graphics_fill_rect(
        window_x + 16, window_y + 58,
        window_w - 32, window_h - 74,
        0x00070A0F
    );

    size_t count = terminal_history_count();
    size_t first = count > 27 ? count - 27 : 0;
    char line[81];
    int text_y = window_y + 70;

    for (size_t i = first;
         i < count && text_y < window_y + window_h - 24;
         i++)
    {
        if (!terminal_history_line(i, line, sizeof(line)))
            continue;

        for (unsigned int j = 0; line[j]; j++)
        {
            if (line[j] >= 'a' && line[j] <= 'z')
                line[j] =
                    (char)(line[j] - 'a' + 'A');
        }

        graphics_draw_text(
            window_x + 26,
            text_y,
            line,
            0x00D7DEE7,
            1
        );

        text_y += 18;
    }

    (void)height;
}

static void terminal_process_step(void)
{
    if (!terminal_window_open)
        return;

    if (!terminal_window_prompted)
    {
        terminal_write(
            "\n[GUI] Terminal opened (PID 1).\n"
        );
        shell_show_prompt();
        terminal_window_prompted = 1;
    }

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

    char character = keyboard_get_char();

    if (character != 0)
    {
        if (character == 27)
        {
            terminal_window_open = 0;
            terminal_window_prompted = 0;

            if (graphics_ready_global)
                graphics_present();

            return;
        }

        shell_handle_char(character);
        changed = 1;
    }

    if (changed)
        terminal_window_draw();
}


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

    graphics_ready_global =
        graphics_initialize(mbd);

    terminal_write("Graphics diagnostics:\n");
    terminal_write("  Multiboot flags: ");
    terminal_write_hex(mbd ? mbd->flags : 0);
    terminal_write("\n");
    terminal_write("  Framebuffer: ");
    terminal_write_hex(mbd ? (uint32_t)mbd->framebuffer_addr : 0);
    terminal_write("\n");
    terminal_write("  Type: ");
    terminal_write_hex(mbd ? mbd->framebuffer_type : 0);
    terminal_write("\n");
    terminal_write("  BPP: ");
    terminal_write_hex(mbd ? mbd->framebuffer_bpp : 0);
    terminal_write("\n");
    terminal_write("  Graphics init: ");
    terminal_write(graphics_ready_global ? "YES\n" : "NO\n");

    heap_initialize();
    filesystem_initialize(mbd);

    process_initialize();

    terminal_pid =
        process_create_kernel(
            "terminal",
            0,
            terminal_process_main
        );

    process_attach_current(
        "desktop",
        0
    );

    boot_memory = read_tsc();
    boot_interrupts = boot_memory;

    mouse_initialize();
    boot_mouse = read_tsc();

    shell_initialize();
    shell_set_multiboot_info(mbd);
    boot_shell = read_tsc();

    __asm__ volatile ("sti");

    terminal_write("\nKernel initialized successfully.\n");
    terminal_write(
        filesystem_is_initialized() ?
        "Filesystem: online.\n" :
        "Filesystem: offline.\n"
    );
    terminal_write(
        process_is_initialized() ?
        "Process scheduler: online.\n" :
        "Process scheduler: offline.\n"
    );
    terminal_write(
        "Paging: enabled (identity-mapped 16 MiB).\n"
    );
    terminal_write(
        "Type 'help' in the Terminal app.\n\n"
    );

    if (graphics_ready_global)
        graphics_present();

    while (1)
    {
        /*
         * Until the low-level context-switch mechanism is complete,
         * the desktop remains the known-good execution context.
         * The terminal process is still represented by PID 1 and
         * receives cooperative execution here rather than risking
         * the graphics stack with an unsafe IRQ stack switch.
         */
        if (terminal_window_open)
            terminal_process_step();

        if (mouse_has_event())
        {
            int wheel_event =
                mouse_has_wheel_event();

            int click_event =
                mouse_has_click_event();

            if (wheel_event)
            {
                int wheel_delta =
                    mouse_get_wheel_delta();

                if (wheel_delta > 0)
                    terminal_scroll_down();

                if (wheel_delta < 0)
                    terminal_scroll_up();
            }

            if (click_event &&
                graphics_get_active_panel() == 3 &&
                !terminal_window_open)
            {
                terminal_window_open = 1;
                terminal_window_prompted = 0;

                process_wake(
                    (uint32_t)terminal_pid
                );

                if (graphics_ready_global)
                {
                    graphics_present();
                    terminal_window_draw();
                }
            }
            else if (graphics_ready_global &&
                     (wheel_event || click_event))
            {
                graphics_present();

                if (terminal_window_open)
                    terminal_window_draw();
            }

            mouse_clear_event_flags();
            continue;
        }

        __asm__ volatile ("hlt");
    }
}

unsigned long long kernel_boot_start(void) { return boot_start; }
unsigned long long kernel_boot_gdt(void) { return boot_gdt; }
unsigned long long kernel_boot_terminal(void) { return boot_terminal; }
unsigned long long kernel_boot_keyboard(void) { return boot_keyboard; }
unsigned long long kernel_boot_mouse(void) { return boot_mouse; }
unsigned long long kernel_boot_interrupts(void) { return boot_interrupts; }
unsigned long long kernel_boot_shell(void) { return boot_shell; }
