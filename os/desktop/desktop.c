#include "desktop.h"

#include "graphics.h"
#include "interrupts.h"
#include "keyboard.h"
#include "mouse.h"
#include "process.h"
#include "terminal_process.h"
#include "shell.h"
#include "terminal.h"
#include "file_explorer.h"

#include <stddef.h>
#include <stdint.h>

static int terminal_window_open;
static int terminal_window_prompted;
static unsigned int terminal_last_blink_state;

static void terminal_window_draw(void);

static void desktop_gui_present(void)
{
    if (!graphics_is_initialized())
        return;

    graphics_present();

    if (terminal_window_open && graphics_get_active_panel() == 3)
        terminal_window_draw();
    else
        graphics_draw_cursor();
}

int desktop_launch_terminal(void)
{
    if (terminal_window_open)
        return 1;

    int pid = terminal_process_start(process_current_pid());

    if (pid < 0)
        return 0;

    terminal_window_open = 1;
    terminal_window_prompted = 1;
    graphics_clear_terminal_close_requested();
    terminal_last_blink_state = interrupts_get_ticks() / 50U;

    graphics_set_terminal_running(1);
    graphics_select_panel(3);
    desktop_gui_present();

    return 1;
}

static void terminal_close_window(void)
{
    terminal_window_open = 0;
    terminal_window_prompted = 0;
    graphics_clear_terminal_open_request();
    file_explorer_initialize();

    graphics_set_terminal_running(0);

    terminal_reset();
    shell_initialize();

    graphics_select_panel(0);
    desktop_gui_present();
}

static void terminal_window_draw(void)
{
    if (!graphics_is_initialized() || !terminal_window_open)
        return;

    int width = (int)graphics_get_width();
    int height = (int)graphics_get_height();

    int window_w = (int)graphics_get_terminal_width();
    int window_h = (int)graphics_get_terminal_height();
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

    graphics_draw_window_frame(window_x, window_y, window_w, window_h, "Terminal");

    /* A compact tab strip separates terminal sessions from window chrome. */
    graphics_fill_rect(window_x + 1, window_y + 46, window_w - 2, 34, 0x00131B25);
    graphics_fill_rounded_rect(window_x + 14, window_y + 50, 156, 26, 6, 0x00212D3A);
    graphics_fill_rect(window_x + 24, window_y + 74, 136, 2, 0x004B9CFF);
    graphics_draw_text(window_x + 28, window_y + 59, "Shell", 0x00E8EDF4, 1);
    graphics_draw_text(window_x + 184, window_y + 59, "VantaOS", 0x007D8DA1, 1);

    int content_x = window_x + 18;
    int content_y = window_y + 88;
    int content_w = window_w - 36;
    int content_h = window_h - 104;

    graphics_fill_rounded_rect(content_x, content_y, content_w, content_h, 5, 0x000B1017);

    size_t count = terminal_history_count();
    size_t visible_lines = (size_t)(content_h / 12);
    if (visible_lines > 55)
        visible_lines = 55;

    size_t live_start = count > visible_lines ? count - visible_lines : 0;
    size_t scroll_offset = terminal_get_scroll_offset();
    if (scroll_offset > live_start)
        scroll_offset = live_start;
    size_t first = live_start - scroll_offset;
    char line[81];
    int text_y = content_y + 8;

    for (size_t i = first; i < count && text_y < content_y + content_h - 4; i++)
    {
        if (!terminal_history_line(i, line, sizeof(line)))
            continue;

        graphics_draw_text(content_x + 14, text_y, line, 0x00DDE5EE, 1);
        text_y += 12;
    }

    size_t cursor_line = terminal_get_cursor_line();
    size_t cursor_column = terminal_get_cursor_column();
    size_t cursor_first = live_start - scroll_offset;

    if (scroll_offset == 0 &&
        cursor_line >= cursor_first &&
        cursor_line < cursor_first + visible_lines)
    {
        int cursor_x = content_x + 14 + (int)cursor_column * 6;
        int cursor_y = content_y + 8 + (int)(cursor_line - cursor_first) * 12;
        graphics_fill_rect(cursor_x, cursor_y, 2, 8, 0x00F2F5F8);
        terminal_window_prompted = 0;
    }

    graphics_draw_cursor();
}

void desktop_initialize(multiboot_info_t* mbd)
{
    (void)mbd;
    terminal_window_open = 0;
    terminal_window_prompted = 0;
    terminal_last_blink_state = 0;
}

void desktop_present(void)
{
    desktop_gui_present();
}

void desktop_update(void)
{
    if (!terminal_window_open && graphics_terminal_open_requested())
    {
        graphics_clear_terminal_open_request();
        if (!desktop_launch_terminal())
            return;
    }

    if (terminal_window_open && graphics_terminal_close_requested())
    {
        terminal_process_request_exit();
        terminal_close_window();
        return;
    }

    if (terminal_window_open)
    {
        if (!terminal_process_is_running())
        {
            terminal_close_window();
        }
        else if (graphics_get_active_panel() == 3)
        {
            if (terminal_process_consume_redraw())
            {
                terminal_window_draw();
            }
            else
            {
                unsigned int blink_state = interrupts_get_ticks() / 50U;
                if (blink_state != terminal_last_blink_state)
                {
                    terminal_last_blink_state = blink_state;
                    terminal_window_draw();
                }
            }
        }
        else
        {
            /* The terminal is minimized; keep the process alive but do not
             * paint the window over the desktop. Keep redraw state for when
             * the terminal is restored from the taskbar. */
        }
    }

    if (mouse_has_event())
    {
        int wheel_event = mouse_has_wheel_event();
        int click_event = mouse_has_click_event();

        if (wheel_event && terminal_window_open && graphics_get_active_panel() == 3)
        {
            int wheel_delta = mouse_get_wheel_delta();
            /*
             * One PS/2 wheel detent is too slow for the GUI terminal.
             * Consume two history lines per detent while keeping the
             * existing direction and scroll model intact.
             */
            if (wheel_delta > 0)
            {
                terminal_scroll_down();
                terminal_scroll_down();
            }
            if (wheel_delta < 0)
            {
                terminal_scroll_up();
                terminal_scroll_up();
            }
            terminal_window_draw();
        }

        if (graphics_is_initialized())
        {
            if (graphics_terminal_is_dragging() ||
                graphics_explorer_is_dragging() ||
                click_event)
                desktop_gui_present();
            else if (mouse_has_move_event())
                graphics_draw_cursor();
        }

        mouse_clear_event_flags();
    }
}
