/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "desktop.h"

#include "graphics.h"
#include "desktop_theme.h"
#include "interrupts.h"
#include "keyboard.h"
#include "mouse.h"
#include "process.h"
#include "terminal_process.h"
#include "shell.h"
#include "terminal.h"
#include "file_explorer_process.h"
#include "file_explorer.h"
#include "graphics_internal.h"
#include "gui_server.h"
#include "usb.h"

#include <stddef.h>
#include <stdint.h>

static int terminal_window_open;
static unsigned int terminal_last_blink_state;
static unsigned int desktop_last_clock_refresh;
static void terminal_close_window(void);

static void desktop_draw_terminal_window(int x, int y, int width, int height)
{
    if (!terminal_window_open || width < 80 || height < 70)
        return;
    const size_t total = terminal_history_count();
    size_t visible = (size_t)(height - 64) / 16U;
    if (visible == 0U) return;
    size_t first = total > visible ? total - visible : 0U;
    size_t scroll = terminal_get_scroll_offset();
    if (scroll > first) first = 0U;
    else first -= scroll;
    int max_chars = (width - 28) / 8;
    if (max_chars > 80) max_chars = 80;
    if (max_chars < 1) return;
    char line[81];
    graphics_fill_rect(x + 8, y + 50, width - 16, height - 58,
                       0x000B111A);
    for (size_t row = 0; row < visible && first + row < total; row++)
    {
        for (size_t i = 0; i < sizeof(line); i++) line[i] = '\0';
        if (terminal_history_line(first + row, line, sizeof(line)) < 0)
            continue;
        line[max_chars] = '\0';
        graphics_draw_text(x + 16, y + 56 + (int)row * 16,
                           line, 0x00D8E2EDU, 1U);
    }
    graphics_fill_rect(x + 16, y + height - 18, 18, 2, 0x005DA9FFU);
}

static int desktop_files_click(int x, int y, int width, int height,
                               int pointer_x, int pointer_y, int button)
{
    if (button != 1) return 0;
    return file_explorer_click_managed(x, y, width, height,
                                       pointer_x, pointer_y);
}

static void desktop_draw_files(int x, int y, int width, int height)
{
    file_explorer_initialize();
    file_explorer_draw_managed(x, y, width, height);
}

static void desktop_draw_system(int x, int y, int width, int height)
{
    graphics_draw_system_page_detail(x, y, width, height);
}

static void desktop_draw_settings(int x, int y, int width, int height)
{
    graphics_draw_settings_page_detail(x, y, width, height);
}

static int desktop_settings_click(int x, int y, int width, int height,
                                  int pointer_x, int pointer_y, int button)
{
    (void)height;
    if (button != 1 || pointer_y < y + 156 || pointer_y >= y + 210)
        return 0;
    for (int i = 0; i < 3; i++)
    {
        int gap = 10;
        int bw = (width - 56 - gap * 2) / 3;
        int bx = x + 28 + i * (bw + gap);
        if (pointer_x >= bx && pointer_x < bx + bw)
        {
            settings_resolution_index = i;
            if (graphics_set_resolution(settings_widths[i],
                                         settings_heights[i]))
                gui_server_reflow_windows();
            return 1;
        }
    }
    return 0;
}

static void desktop_close_terminal(void)
{
    if (terminal_window_open)
    {
        terminal_process_request_exit();
        terminal_close_window();
    }
}

static void desktop_register_builtin_windows(void)
{
    int screen_w = (int)graphics_get_width();
    int screen_h = (int)graphics_get_height();
    int work_h = screen_h - 64;
    if (work_h < 160) work_h = screen_h;
    int files_w = screen_w - 32;
    int files_h = screen_h - 120;
    if (files_w > 960) files_w = 960;
    if (files_h > 560) files_h = 560;
    if (files_w < 240) files_w = 240;
    if (files_h < 160) files_h = 160;
    int app_w = screen_w - 32;
    int app_h = work_h - 48;
    if (app_w > 760) app_w = 760;
    if (app_h > 440) app_h = 440;
    if (app_w < 240) app_w = 240;
    if (app_h < 160) app_h = 160;
    int term_w = screen_w - 32;
    int term_h = work_h - 64;
    if (term_w > 800) term_w = 800;
    if (term_h > 480) term_h = 480;
    if (term_w < 240) term_w = 240;
    if (term_h < 160) term_h = 160;

    (void)gui_server_register_builtin("Files",
        (screen_w - files_w) / 2, (work_h - files_h) / 2,
        files_w, files_h, desktop_draw_files, desktop_files_click, 0);
    (void)gui_server_register_builtin("Terminal",
        (screen_w - term_w) / 2, (work_h - term_h) / 2,
        term_w, term_h, desktop_draw_terminal_window, 0,
        desktop_close_terminal);
    (void)gui_server_register_builtin("System",
        (screen_w - app_w) / 2, (work_h - app_h) / 2,
        app_w, app_h, desktop_draw_system, 0, 0);
    (void)gui_server_register_builtin("Settings",
        (screen_w - app_w) / 2 + 24, (work_h - app_h) / 2 + 18,
        app_w, app_h, desktop_draw_settings, desktop_settings_click, 0);
}

static void desktop_gui_present(void)
{
    if (!graphics_is_initialized())
        return;

    gui_server_sync_active_panel();
    graphics_begin_frame();
    if (!graphics_frame_is_active())
    {
        /*
         * Fail closed if the shadow surface cannot be provisioned. Drawing
         * directly into scanout would reintroduce tearing/flicker. Keep the
         * last complete frame visible and retry on the next desktop update.
         */
        return;
    }

    graphics_present();

    gui_server_draw_windows();
    graphics_draw_start_menu_overlay();
    graphics_draw_cursor();
    graphics_end_frame();
}

/*
 * Terminal text changes do not alter the wallpaper or taskbar. Redraw the
 * retained windows on the compositor's shadow surface and commit only their
 * damaged tiles. Never paint the terminal directly into the scanout buffer.
 */
static void desktop_gui_present_terminal_fast(void)
{
    gui_server_sync_active_panel();
    if (!graphics_can_redraw_windows_incrementally())
    {
        desktop_gui_present();
        return;
    }

    graphics_begin_frame();
    if (!graphics_frame_is_active())
    {
        /* Keep the last complete frame; never fall back to direct scanout. */
        return;
    }

    graphics_cursor_restore();
    gui_server_draw_windows();
    graphics_draw_cursor();
    graphics_end_frame();
}

int desktop_launch_terminal(void)
{
    if (terminal_window_open)
    {
        (void)gui_server_set_builtin_visible("Terminal", 1);
        graphics_select_panel(3);
        desktop_gui_present();
        return 1;
    }

    int pid = terminal_process_start(process_current_pid());

    if (pid < 0)
        return 0;

    terminal_window_open = 1;
    graphics_clear_terminal_close_requested();
    terminal_last_blink_state = interrupts_get_ticks() / 50U;

    graphics_set_terminal_running(1);
    graphics_select_panel(3);
    (void)gui_server_set_builtin_visible("Terminal", 1);
    desktop_gui_present();

    return 1;
}

static void terminal_close_window(void)
{
    terminal_window_open = 0;
    graphics_clear_terminal_open_request();
    graphics_set_terminal_running(0);

    terminal_reset();
    shell_initialize();

    graphics_select_panel(0);
    (void)gui_server_set_builtin_visible("Terminal", 0);
    desktop_gui_present();
}

void desktop_initialize(multiboot_info_t* mbd)
{
    (void)mbd;
    terminal_window_open = 0;
    terminal_last_blink_state = 0;
    desktop_last_clock_refresh = 0;
    (void)desktop_theme_reload();
    desktop_register_builtin_windows();
}

void desktop_present(void)
{
    desktop_gui_present();
}

void desktop_update(void)
{
    /* USB polling is owned by desktop_process_main after frame/input work. */
    gui_server_cleanup_dead_windows();
    gui_server_sync_active_panel();

    /* The physical Super/GUI key is a desktop shortcut, not text input. */
    if (keyboard_take_super_event())
    {
        graphics_toggle_start_menu();
        desktop_gui_present();
    }

    /* Keep the RTC clock/date current even when the user is not moving the mouse. */
    if (graphics_get_active_panel() != 3)
    {
        unsigned int refresh = interrupts_get_ticks() / 50U;
        if (refresh != desktop_last_clock_refresh)
        {
            desktop_last_clock_refresh = refresh;
            desktop_gui_present();
        }
    }

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
                desktop_gui_present_terminal_fast();
            }
            else
            {
                unsigned int blink_state = interrupts_get_ticks() / 50U;
                if (blink_state != terminal_last_blink_state)
                {
                    terminal_last_blink_state = blink_state;
                    desktop_gui_present_terminal_fast();
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
        int click_event = mouse_has_click_event();

        if (graphics_is_initialized())
        {
            if (graphics_terminal_is_dragging() ||
                graphics_explorer_is_dragging() ||
                click_event)
            {
                graphics_invalidate_scene();
                desktop_gui_present();
            }
            else if (mouse_has_move_event())
            {
                /*
                 * Pointer-only motion uses the shadow surface and damage
                 * tracker. Cursor save-under is restored in the shadow buffer
                 * and only the old/new cursor tiles are committed to scanout.
                 */
                graphics_update_cursor();
            }
        }

        mouse_clear_event_flags();
    }

    if (gui_server_take_redraw_request())
    {
        graphics_invalidate_scene();
        desktop_gui_present();
    }
}
