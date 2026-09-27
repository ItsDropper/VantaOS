#include "desktop.h"

#include "graphics.h"
#include "interrupts.h"
#include "keyboard.h"
#include "mouse.h"
#include "process.h"
#include "shell.h"
#include "terminal.h"

#include <stddef.h>
#include <stdint.h>

static int terminal_window_open;
static int terminal_window_prompted;
static int terminal_pid = -1;
static int files_pid = -1;

static void terminal_window_draw(void);
static void terminal_process_step(void);
static void files_process_step(void);

static void desktop_gui_present(void)
{
    if (!graphics_is_initialized())
        return;

    graphics_present();

    if (terminal_window_open &&
        graphics_get_active_panel() == 3)
        terminal_window_draw();
    else
        graphics_draw_cursor();
}

static int desktop_try_launch_terminal(void)
{
    if (terminal_window_open)
        return 1;

    if (terminal_pid < 0 ||
        process_get((uint32_t)terminal_pid) == NULL ||
        process_get((uint32_t)terminal_pid)->state == PROCESS_TERMINATED)
    {
        terminal_pid = process_create_kernel(
            "terminal",
            process_current_pid(),
            terminal_process_step
        );
    }

    if (terminal_pid < 0)
        return 0;

    terminal_reset();
    shell_initialize();

    terminal_window_open = 1;
    terminal_window_prompted = 0;

    graphics_set_terminal_running(1);
    graphics_select_panel(3);
    process_mark_running((uint32_t)terminal_pid);

    desktop_gui_present();
    return 1;
}

static void terminal_open_window(void)
{
    (void)desktop_try_launch_terminal();
}

static void terminal_close_window(void)
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
    desktop_gui_present();
}

static void files_process_step(void)
{
    if (graphics_get_active_panel() != 2)
        return;
}

static void terminal_window_draw(void)
{
    if (!graphics_is_initialized() || !terminal_window_open)
        return;

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

    graphics_fill_rect(
        window_x, window_y, window_w, window_h,
        0x000B1118
    );

    graphics_fill_rect(
        window_x, window_y, window_w, 44,
        0x0017222D
    );

    graphics_fill_rect(
        window_x, window_y + 43, window_w, 1,
        0x002B80C9
    );

    graphics_fill_rect(
        window_x + 14, window_y + 10, 24, 24,
        0x001E3A50
    );

    graphics_draw_text(
        window_x + 20, window_y + 16,
        "V", 0x005AA9E6, 1
    );

    graphics_draw_text(
        window_x + 48, window_y + 14,
        "Terminal", 0x00F2F5F8, 1
    );

    graphics_draw_text(
        window_x + 48, window_y + 27,
        "VantaOS", 0x007F95A8, 1
    );

    graphics_draw_text(
        window_x + window_w - 118, window_y + 14,
        "_", 0x00B7C5D1, 1
    );

    graphics_draw_text(
        window_x + window_w - 84, window_y + 13,
        "[]", 0x00B7C5D1, 1
    );

    graphics_draw_text(
        window_x + window_w - 38, window_y + 13,
        "X", 0x00F2F5F8, 1
    );

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
        "VantaOS", 0x00E7EEF4, 1
    );

    int content_x = window_x + 18;
    int content_y = window_y + 88;
    int content_w = window_w - 36;
    int content_h = window_h - 104;

    graphics_fill_rect(
        content_x, content_y,
        content_w, content_h,
        0x000B1118
    );

    size_t count = terminal_history_count();
    size_t visible_lines = (size_t)(content_h / 10);

    if (visible_lines > 55)
        visible_lines = 55;

    size_t first =
        count > visible_lines ?
        count - visible_lines : 0;

    char line[81];
    int text_y = content_y + 8;

    for (size_t i = first;
         i < count &&
         text_y < content_y + content_h - 4;
         i++)
    {
        if (!terminal_history_line(i, line, sizeof(line)))
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

    size_t cursor_line = terminal_get_cursor_line();
    size_t cursor_column = terminal_get_cursor_column();

    size_t cursor_first =
        count > visible_lines ?
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
            cursor_x, cursor_y, 2, 8,
            0x00F2F5F8
        );
    }

    graphics_draw_cursor();
}

static void terminal_process_step(void)
{
    if (!terminal_window_open)
        return;

    if (!terminal_window_prompted)
    {
        terminal_write("\nVantaOS Terminal\n");
        shell_show_prompt();
        terminal_window_prompted = 1;
    }

    int changed = 0;

    if (keyboard_has_event())
    {
        keyboard_event_t event = keyboard_get_event();

        if (event == KEY_EVENT_TERMINAL)
            return;

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
            terminal_close_window();
            return;
        }

        shell_handle_char(character);
        changed = 1;
    }

    if (changed)
        terminal_window_draw();
}

void desktop_initialize(multiboot_info_t* mbd)
{
    (void)mbd;

    terminal_window_open = 0;
    terminal_window_prompted = 0;
    terminal_pid = -1;
    files_pid = -1;

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
}

void desktop_present(void)
{
    desktop_gui_present();
}

void desktop_update(void)
{
    /*
     * Desktop-level keyboard events are consumed before application
     * input. This makes the Terminal shortcut an actual desktop action
     * instead of something the shell can accidentally consume.
     */
    if (!terminal_window_open && keyboard_has_event())
    {
        keyboard_event_t event = keyboard_get_event();

        if (event == KEY_EVENT_TERMINAL)
        {
            desktop_try_launch_terminal();
            return;
        }

        /*
         * Preserve unrelated desktop events. They belong to the
         * application layer once an application is active.
         */
    }

    /*
     * Mouse handlers select the application panel immediately from
     * the input interrupt. Launch the selected desktop application
     * here on the next kernel iteration.
     */
    if (graphics_get_active_panel() == 3 &&
        !terminal_window_open)
    {
        if (!desktop_try_launch_terminal())
            return;
    }

    if (terminal_window_open &&
        graphics_terminal_close_requested())
    {
        terminal_close_window();
        return;
    }

    if (files_pid >= 0)
    {
        if (graphics_get_active_panel() == 2)
        {
            process_mark_running((uint32_t)files_pid);
            files_process_step();
            process_mark_ready((uint32_t)files_pid);
        }
        else
        {
            process_mark_ready((uint32_t)files_pid);
        }
    }

    if (terminal_window_open)
    {
        terminal_process_step();

        if (terminal_pid >= 0 && terminal_window_open)
            process_mark_ready((uint32_t)terminal_pid);
    }
    else if (terminal_pid >= 0)
    {
        process_mark_ready((uint32_t)terminal_pid);
    }

    if (mouse_has_event())
    {
        int wheel_event = mouse_has_wheel_event();
        int click_event = mouse_has_click_event();

        if (wheel_event)
        {
            int wheel_delta = mouse_get_wheel_delta();

            if (wheel_delta > 0)
                terminal_scroll_down();

            if (wheel_delta < 0)
                terminal_scroll_up();
        }

        if (click_event &&
            graphics_get_active_panel() == 3 &&
            terminal_pid >= 0)
        {
            process_wake((uint32_t)terminal_pid);
        }

        if (graphics_is_initialized())
        {
            if (graphics_terminal_is_dragging() ||
                click_event ||
                (wheel_event && terminal_window_open))
                desktop_gui_present();
            else if (mouse_has_move_event())
                graphics_draw_cursor();
        }

        mouse_clear_event_flags();
    }
}
