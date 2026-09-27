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
static int files_pid = -1;
static int graphics_ready_global = 0;

static void terminal_window_draw(void);
static void terminal_process_step(void);
static void files_process_step(void);

static void gui_present(void)
{
    if (!graphics_ready_global)
        return;

    graphics_present();

    if (terminal_window_open &&
        graphics_get_active_panel() == 3)
        terminal_window_draw();
    else
        graphics_draw_cursor();
}

static void terminal_open_window(void)
{
    if (terminal_pid < 0)
    {
        terminal_pid = process_create_kernel(
            "terminal",
            process_current_pid(),
            terminal_process_step
        );

        if (terminal_pid < 0)
            return;
    }

    terminal_reset();
    shell_initialize();

    terminal_window_open = 1;
    terminal_window_prompted = 0;
    graphics_set_terminal_running(1);
    graphics_select_panel(3);

    /* This kernel still uses cooperative execution, so the terminal
     * is explicitly marked active without changing the desktop's
     * actual CPU context. */
    process_mark_running((uint32_t)terminal_pid);
    gui_present();
}

static void files_process_step(void)
{
    /*
     * Files is a desktop application process. Filesystem operations
     * are performed by the real Files window handlers; this cooperative
     * step is the application lifecycle hook until CPU context switching
     * is implemented.
     */
    if (graphics_get_active_panel() != 2)
        return;
}

static void terminal_window_draw(void)
{
    if (!graphics_ready_global || !terminal_window_open)
        return;

    /*
     * This is a real terminal surface, not a fake status panel:
     * dark content area, title/tab row, monospace grid, prompt,
     * scrolling history and an application cursor.
     *
     * Windows Terminal uses a dark title/tab row and black terminal
     * content by default; VantaOS follows that visual model here.
     */
    int width = (int)graphics_get_width();
    int height = (int)graphics_get_height();

    int window_w = graphics_terminal_is_maximized() ? width : 880;
    int window_h = graphics_terminal_is_maximized() ? height : 620;
    int window_x = graphics_terminal_is_maximized() ? 0 : graphics_get_terminal_x();
    int window_y = graphics_terminal_is_maximized() ? 0 : graphics_get_terminal_y();

    if (!graphics_terminal_is_maximized() && window_w > width - 20)
    {
        window_w = width - 20;
        window_x = 10;
    }

    if (!graphics_terminal_is_maximized() && window_h > height - 20)
    {
        window_h = height - 20;
        window_y = 10;
    }

    /* Clean Vanta terminal chrome. Every visible control has a real action. */
    graphics_fill_rect(
        window_x + 8, window_y + 10,
        window_w, window_h,
        0x00000000
    );

    graphics_fill_rect(
        window_x, window_y,
        window_w, window_h,
        0x000B1118
    );

    /* Title bar. */
    graphics_fill_rect(
        window_x, window_y,
        window_w, 44,
        0x0017222D
    );

    graphics_fill_rect(
        window_x, window_y + 43,
        window_w, 1,
        0x002B80C9
    );

    /* Terminal identity. */
    graphics_fill_rect(
        window_x + 14, window_y + 10,
        24, 24,
        0x001E3A50
    );

    graphics_draw_text(
        window_x + 20, window_y + 16,
        "V",
        0x005AA9E6, 1
    );

    graphics_draw_text(
        window_x + 48, window_y + 14,
        "Terminal",
        0x00F2F5F8, 1
    );

    graphics_draw_text(
        window_x + 48, window_y + 27,
        "VantaOS",
        0x007F95A8, 1
    );

    /* Window controls: minimize, maximize/restore, close. */
    graphics_draw_text(
        window_x + window_w - 118, window_y + 14,
        "_",
        0x00B7C5D1, 1
    );

    graphics_draw_text(
        window_x + window_w - 84, window_y + 13,
        "[]",
        0x00B7C5D1, 1
    );

    graphics_draw_text(
        window_x + window_w - 38, window_y + 13,
        "X",
        0x00F2F5F8, 1
    );

    /* Thin terminal tab strip. */
    graphics_fill_rect(
        window_x, window_y + 44,
        window_w, 34,
        0x000E171F
    );

    graphics_fill_rect(
        window_x + 14, window_y + 49,
        180, 29,
        0x001A2A37
    );

    graphics_fill_rect(
        window_x + 14, window_y + 76,
        180, 2,
        0x003B82F6
    );

    graphics_draw_text(
        window_x + 28, window_y + 58,
        "VantaOS",
        0x00E7EEF4, 1
    );

    /* Terminal content. */
    int content_x = window_x + 18;
    int content_y = window_y + 88;
    int content_w = window_w - 36;
    int content_h = window_h - 104;

    graphics_fill_rect(
        content_x, content_y,
        content_w, content_h,
        0x000B1118
    );

    /*
     * The existing shell history is the terminal's backing store.
     * One graphics character cell is 6x8 at scale 1, giving the
     * terminal a dense monospace layout instead of a giant UI font.
     */
    size_t count = terminal_history_count();
    size_t visible_lines = (size_t)(content_h / 10);

    if (visible_lines > 55)
        visible_lines = 55;

    size_t first = count > visible_lines ?
        count - visible_lines : 0;

    char line[81];
    int text_y = content_y + 8;

    for (size_t i = first;
         i < count && text_y < content_y + content_h - 4;
         i++)
    {
        if (!terminal_history_line(
                i, line, sizeof(line)))
            continue;

        graphics_draw_text(
            content_x + 10,
            text_y,
            line,
            0x00F2F2F2,
            1
        );

        text_y += 10;
    }

    /*
     * Draw a terminal-style bar cursor at the shell's actual
     * editing position.  Blink is driven by the kernel tick
     * counter rather than continuously repainting the window.
     */
    size_t cursor_line = terminal_get_cursor_line();
    size_t cursor_column = terminal_get_cursor_column();

    size_t cursor_first = count > visible_lines ?
        count - visible_lines : 0;

    if (cursor_line >= cursor_first &&
        cursor_line < cursor_first + visible_lines &&
        ((interrupts_get_ticks() / 50) & 1U) == 0)
    {
        int cursor_x =
            content_x + 10 + (int)cursor_column * 6;

        int cursor_y =
            content_y + 8 +
            (int)(cursor_line - cursor_first) * 10;

        graphics_fill_rect(
            cursor_x,
            cursor_y,
            2,
            8,
            0x00F2F2F2
        );
    }

    /* Keep the mouse cursor above the terminal window. */
    graphics_draw_cursor();
}

static void terminal_process_step(void)
{
    if (!terminal_window_open)
        return;

    if (!terminal_window_prompted)
    {
        terminal_write(
            "\nVantaOS Terminal\n"
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
            graphics_set_terminal_running(0);

            if (graphics_ready_global)
            {
                graphics_select_panel(0);
                gui_present();
            }

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

    process_attach_current(
        "desktop",
        0
    );

    /*
     * Desktop applications have persistent process records. They remain
     * READY while closed and are marked RUNNING while their application
     * surface is active.
     */
    terminal_pid = process_create_kernel(
        "terminal",
        process_current_pid(),
        terminal_process_step
    );

    files_pid = process_create_kernel(
        "files",
        process_current_pid(),
        files_process_step
    );

    if (terminal_pid < 0 || files_pid < 0)
        terminal_write("Application process initialization failed.\n");

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
        "Process manager: online.\n" :
        "Process manager: offline.\n"
    );
    terminal_write(
        "Paging: enabled (identity-mapped 16 MiB).\n"
    );
    terminal_write(
        "Type 'help' in the Terminal app.\n\n"
    );

    gui_present();

    while (1)
    {
        /*
         * The desktop remains the execution context until the
         * low-level context switch path is complete.  PID 1 is
         * the terminal application record and its work is run
         * cooperatively from the stable desktop context.
         */
        /*
         * Launch the Terminal directly from the card selection.
         * Do not rebuild the desktop first: the terminal window
         * draws over the existing desktop surface in one pass.
         */
        if (graphics_get_active_panel() == 3 &&
            !terminal_window_open)
        {
            terminal_open_window();
        }

        if (terminal_window_open &&
            graphics_terminal_close_requested())
        {
            terminal_window_open = 0;
            terminal_window_prompted = 0;
            graphics_set_terminal_running(0);

            if (terminal_pid >= 0)
            {
                process_terminate((uint32_t)terminal_pid);
                terminal_pid = -1;
            }

            terminal_reset();
            shell_initialize();
            graphics_select_panel(0);
            gui_present();
        }

        if (graphics_get_active_panel() == 2 &&
            files_pid >= 0)
        {
            process_mark_running((uint32_t)files_pid);
            files_process_step();
            process_mark_ready((uint32_t)files_pid);
        }
        else if (files_pid >= 0)
        {
            process_mark_ready((uint32_t)files_pid);
        }

        if (terminal_window_open)
        {
            if (terminal_pid >= 0)
                process_mark_running((uint32_t)terminal_pid);

            terminal_process_step();

            if (terminal_pid >= 0 && terminal_window_open)
                process_mark_ready((uint32_t)terminal_pid);
        }
        else if (terminal_pid >= 0)
        {
            process_mark_ready((uint32_t)terminal_pid);
        }

        /*
         * The GUI must remain usable even when the relative PS/2
         * pointer is not aligned with the host pointer.  T is the
         * keyboard launch shortcut for the Terminal application.
         */
        if (!terminal_window_open && keyboard_has_event())
        {
            keyboard_event_t desktop_event = keyboard_get_event();

            if (desktop_event == KEY_EVENT_TERMINAL)
            {
                terminal_open_window();
                continue;
            }
        }

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
                graphics_get_active_panel() == 3)
            {
                process_wake(
                    (uint32_t)terminal_pid
                );
            }

            if (graphics_ready_global)
            {
                /*
                 * Normal pointer movement only redraws the cursor.
                 * Full-frame rendering is reserved for actual UI
                 * changes and window dragging.
                 */
                if (graphics_terminal_is_dragging() ||
                    click_event ||
                    (wheel_event && terminal_window_open))
                {
                    gui_present();
                }
                else if (mouse_has_move_event())
                {
                    graphics_draw_cursor();
                }
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