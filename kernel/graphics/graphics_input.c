/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "graphics_internal.h"
#include "filesystem.h"
#include "terminal.h"
#include "process.h"
#include "file_explorer.h"
#include "pmm.h"
#include "gui_server.h"

/* Explorer window state used by mouse interaction. */
extern int explorer_dragging;
extern int explorer_x;
extern int explorer_y;
extern int explorer_drag_offset_x;
extern int explorer_drag_offset_y;
extern int explorer_maximized;
extern int explorer_restore_x;
extern int explorer_restore_y;

void graphics_toggle_start_menu(void)
{
    if (!initialized)
        return;

    start_menu_open = !start_menu_open;
    graphics_invalidate_scene();
    explorer_dragging = 0;
    terminal_dragging = 0;
}

void graphics_mouse_click(int button)
{
    if (!initialized || button != 1)
        return;

    int width = (int)framebuffer_width;
    int height = (int)framebuffer_height;
    int taskbar_y = height - 64;

    /*
     * Taskbar owns task switching. This must run before an active-window
     * handler, otherwise the Explorer window consumes clicks that are
     * actually on the taskbar.
     */
    if (cursor_y >= taskbar_y + 8 && cursor_y < taskbar_y + 56)
    {
        int center = width / 2;

        if (cursor_x >= center - 108 && cursor_x < center - 66)
        {
            graphics_toggle_start_menu();
            return;
        }

        if (cursor_x >= center + 18 && cursor_x < center + 60)
        {
            file_explorer_initialize();
            (void)gui_server_set_builtin_visible("Files", 1);
            active_panel = 2;
            return;
        }

        if (cursor_x >= center + 66 && cursor_x < center + 108)
        {
            active_panel = 3;
            explorer_dragging = 0;
            graphics_request_terminal_open();
            return;
        }

        if (gui_server_taskbar_click(cursor_x, cursor_y, taskbar_y, width))
            return;
    }

    if (start_menu_open)
    {
        int menu_w = width - 32;
        int menu_h = height - 72;

        if (menu_w > 460)
            menu_w = 460;
        if (menu_h > 500)
            menu_h = 500;

        int menu_x = width / 2 - menu_w / 2;
        int menu_y = height - menu_h - 8;

        if (cursor_x >= menu_x + 24 && cursor_x < menu_x + 436 &&
            cursor_y >= menu_y + 110 && cursor_y < menu_y + 164)
        {
            active_panel = 3;
            graphics_request_terminal_open();
            start_menu_open = 0;
            return;
        }

        if (cursor_x >= menu_x + 24 && cursor_x < menu_x + 436 &&
            cursor_y >= menu_y + 164 && cursor_y < menu_y + 218)
        {
            active_panel = 2;
            file_explorer_initialize();
            (void)gui_server_set_builtin_visible("Files", 1);
            start_menu_open = 0;
            return;
        }

        if (cursor_x >= menu_x + 24 && cursor_x < menu_x + 436 &&
            cursor_y >= menu_y + 218 && cursor_y < menu_y + 272)
        {
            active_panel = 1;
            (void)gui_server_set_builtin_visible("System", 1);
            start_menu_open = 0;
            return;
        }

        if (cursor_x >= menu_x + 24 && cursor_x < menu_x + 436 &&
            cursor_y >= menu_y + 272 && cursor_y < menu_y + 326)
        {
            active_panel = 4;
            (void)gui_server_set_builtin_visible("Settings", 1);
            start_menu_open = 0;
            return;
        }

        if (!(cursor_x >= menu_x && cursor_x < menu_x + menu_w &&
              cursor_y >= menu_y && cursor_y < height - 8))
            start_menu_open = 0;

        return;
    }

    /* Route window input after taskbar and Start-menu overlay hit testing. */
    if (gui_server_mouse_click(cursor_x, cursor_y, button))
        return;

    /*
     * Explorer has one input owner. Its hit-testing, title-bar controls,
     * sidebar, toolbar and file list all use the same geometry as drawing.
     */
    if (cursor_x >= 24 && cursor_x < 112 &&
        cursor_y >= 58 && cursor_y < 130)
    {
        active_panel = 1;
        (void)gui_server_set_builtin_visible("System", 1);
        return;
    }

    if (cursor_x >= 120 && cursor_x < 208 &&
        cursor_y >= 58 && cursor_y < 130)
    {
        active_panel = 2;
        file_explorer_initialize();
        (void)gui_server_set_builtin_visible("Files", 1);
        return;
    }

    if (cursor_x >= 216 && cursor_x < 304 &&
        cursor_y >= 58 && cursor_y < 130)
    {
        active_panel = 3;
        graphics_request_terminal_open();
        return;
    }
}

void graphics_mouse_move(int dx, int dy)
{
    if (!initialized)
        return;

    cursor_x += dx;
    cursor_y -= dy;

    if (cursor_x < 0)
        cursor_x = 0;
    if (cursor_y < 0)
        cursor_y = 0;

    if (cursor_x >= (int)framebuffer_width)
        cursor_x = (int)framebuffer_width - 1;
    if (cursor_y >= (int)framebuffer_height)
        cursor_y = (int)framebuffer_height - 1;

    /* Ring-3 GUI windows have independent z-order and drag state. */
    gui_server_mouse_move(cursor_x, cursor_y);

    if ((explorer_dragging && !explorer_maximized) ||
        (terminal_dragging && !terminal_maximized))
        graphics_invalidate_scene();

    if (explorer_dragging && !explorer_maximized)
    {
        explorer_x = cursor_x - explorer_drag_offset_x;
        explorer_y = cursor_y - explorer_drag_offset_y;

        int ew, eh, ex, ey;

        file_explorer_window_geometry(
            (int)framebuffer_width,
            (int)framebuffer_height,
            &ex,
            &ey,
            &ew,
            &eh
        );

        if (explorer_x < 0)
            explorer_x = 0;
        if (explorer_y < 0)
            explorer_y = 0;

        if (explorer_x + ew > (int)framebuffer_width)
            explorer_x = (int)framebuffer_width - ew;

        if (explorer_y + eh > (int)framebuffer_height)
            explorer_y = (int)framebuffer_height - eh;

        if (explorer_x < 0)
            explorer_x = 0;
        if (explorer_y < 0)
            explorer_y = 0;
    }

    if (terminal_dragging && !terminal_maximized)
    {
        terminal_x = cursor_x - terminal_drag_offset_x;
        terminal_y = cursor_y - terminal_drag_offset_y;

        if (terminal_x < 0)
            terminal_x = 0;
        if (terminal_y < 0)
            terminal_y = 0;

        int drag_width = (int)graphics_get_terminal_width();
        int drag_height = (int)graphics_get_terminal_height();

        if (drag_width > (int)framebuffer_width)
            drag_width = (int)framebuffer_width;
        if (drag_height > (int)framebuffer_height)
            drag_height = (int)framebuffer_height;

        if (terminal_x + drag_width > (int)framebuffer_width)
            terminal_x = (int)framebuffer_width - drag_width;

        if (terminal_y + drag_height > (int)framebuffer_height)
            terminal_y = (int)framebuffer_height - drag_height;

        if (terminal_x < 0)
            terminal_x = 0;
        if (terminal_y < 0)
            terminal_y = 0;
    }

}

void graphics_mouse_release(int button)
{
    if (!initialized || button != 1)
        return;

    terminal_dragging = 0;
    explorer_dragging = 0;
    gui_server_mouse_release(button);
}

void graphics_cursor_restore(void)
{
    if (!initialized || !cursor_saved_valid)
        return;

    /* Cursor save-under writes bypass the drawing primitives, so explicitly
     * mark the old cursor footprint when restoring it into the shadow buffer. */
    graphics_mark_damage(cursor_saved_x, cursor_saved_y,
                         cursor_saved_width, cursor_saved_height);
    for (int y = 0; y < cursor_saved_height; y++)
    {
        volatile uint32_t* row =
            (volatile uint32_t*)(
                framebuffer +
                (cursor_saved_y + y) * framebuffer_pitch
            );

        for (int x = 0; x < cursor_saved_width; x++)
            row[cursor_saved_x + x] =
                cursor_saved[y * CURSOR_SAVE_SIZE + x];
    }

    cursor_saved_valid = 0;
}

void graphics_draw_cursor(void)
{
    if (!initialized)
        return;

    int x = cursor_x;
    int y = cursor_y;

    graphics_cursor_restore();

    cursor_saved_x = x;
    cursor_saved_y = y;
    cursor_saved_width = CURSOR_SAVE_SIZE;
    cursor_saved_height = CURSOR_SAVE_SIZE;

    if (cursor_saved_x + cursor_saved_width > (int)framebuffer_width)
        cursor_saved_width = (int)framebuffer_width - cursor_saved_x;

    if (cursor_saved_y + cursor_saved_height > (int)framebuffer_height)
        cursor_saved_height = (int)framebuffer_height - cursor_saved_y;

    if (cursor_saved_width <= 0 || cursor_saved_height <= 0)
        return;

    for (int py = 0; py < cursor_saved_height; py++)
    {
        volatile uint32_t* row =
            (volatile uint32_t*)(
                framebuffer +
                (cursor_saved_y + py) * framebuffer_pitch
            );

        for (int px = 0; px < cursor_saved_width; px++)
            cursor_saved[py * CURSOR_SAVE_SIZE + px] =
                row[cursor_saved_x + px];
    }

    cursor_saved_valid = 1;

    graphics_fill_rect(x, y, 2, 19, 0x00000000);
    graphics_fill_rect(x, y, 4, 2, 0x00000000);
    graphics_fill_rect(x + 2, y + 2, 4, 2, 0x00000000);
    graphics_fill_rect(x + 4, y + 4, 4, 2, 0x00000000);
    graphics_fill_rect(x + 6, y + 6, 4, 2, 0x00000000);
    graphics_fill_rect(x + 8, y + 8, 4, 2, 0x00000000);
    graphics_fill_rect(x + 10, y + 10, 4, 2, 0x00000000);
    graphics_fill_rect(x + 12, y + 12, 3, 2, 0x00000000);
    graphics_fill_rect(x + 12, y + 14, 2, 5, 0x00000000);
    graphics_fill_rect(x + 10, y + 16, 3, 2, 0x00000000);

    graphics_fill_rect(x + 2, y + 2, 2, 13, 0x00FFFFFF);
    graphics_fill_rect(x + 4, y + 4, 2, 13, 0x00FFFFFF);
    graphics_fill_rect(x + 6, y + 6, 2, 11, 0x00FFFFFF);
    graphics_fill_rect(x + 8, y + 8, 2, 9, 0x00FFFFFF);
    graphics_fill_rect(x + 10, y + 10, 2, 6, 0x00FFFFFF);
    graphics_fill_rect(x + 12, y + 12, 1, 3, 0x00FFFFFF);
}
