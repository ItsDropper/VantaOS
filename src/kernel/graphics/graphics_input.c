#include "graphics_internal.h"
#include "filesystem.h"
#include "terminal.h"
#include "process.h"
#include "file_explorer.h"
#include "pmm.h"

#define FILES_WINDOW_MARGIN 24

void graphics_mouse_click(int button)
{
    if (!initialized || button != 1)
        return;

    int width = (int)framebuffer_width;
    int height = (int)framebuffer_height;
    int taskbar_y = height - 64;

    if (start_menu_open)
    {
        int menu_w = width-32; if(menu_w>460) menu_w=460;
        int menu_h = height-72; if(menu_h>500) menu_h=500;
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
            start_menu_open = 0;
            return;
        }

        if (cursor_x >= menu_x + 24 && cursor_x < menu_x + 436 &&
            cursor_y >= menu_y + 218 && cursor_y < menu_y + 272)
        {
            active_panel = 1;
            start_menu_open = 0;
            return;
        }
        if (cursor_x >= menu_x + 24 && cursor_x < menu_x + 436 &&
            cursor_y >= menu_y + 272 && cursor_y < menu_y + 326)
        {
            active_panel = 4;
            start_menu_open = 0;
            return;
        }

        if (!(cursor_x >= menu_x && cursor_x < menu_x + menu_w &&
              cursor_y >= menu_y && cursor_y < height - 8))
            start_menu_open = 0;

        return;
    }

    /*
     * Explorer owns its complete input surface while it is active.
     * Do not duplicate its window geometry here: the renderer and the
     * Explorer hit-testing must have exactly one source of truth.
     * The old outer rectangle could reject valid clicks when the display
     * geometry changed, causing a redraw with no UI action.
     */
    if (active_panel == 2)
    {
        if (file_explorer_click(
                cursor_x, cursor_y, width, height))
        {
            active_panel = 0;
        }

        return;
    }

    if (cursor_y >= taskbar_y + 8 && cursor_y < taskbar_y + 56)
    {
        int center = width / 2;

        if (cursor_x >= center - 190 && cursor_x < center - 142)
        {
            start_menu_open = 1;
            return;
        }

        if (cursor_x >= center - 132 && cursor_x < center - 64)
        {
            active_panel = 2;
            file_explorer_initialize();
            return;
        }

        if (cursor_x >= center - 56 && cursor_x < center + 12)
        {
            active_panel = 3;
            graphics_request_terminal_open();
            return;
        }
    }

    if (active_panel == 3)
    {
        int terminal_w = (int)graphics_get_terminal_width();
        int terminal_h = (int)graphics_get_terminal_height();
        int terminal_x_current = terminal_maximized ? 0 : terminal_x;
        int terminal_y_current = terminal_maximized ? 0 : terminal_y;

        if (!terminal_maximized && terminal_w > width - 20)
        {
            terminal_w = width - 20;
            terminal_x_current = 10;
        }

        if (!terminal_maximized && terminal_h > height - 20)
        {
            terminal_h = height - 20;
            terminal_y_current = 10;
        }

        /* Minimize: keep the terminal process alive and hide its window. */
        if (cursor_x >= terminal_x_current + terminal_w - 140 &&
            cursor_x < terminal_x_current + terminal_w - 96 &&
            cursor_y >= terminal_y_current + 4 &&
            cursor_y < terminal_y_current + 42)
        {
            active_panel = 0;
            terminal_dragging = 0;
    explorer_dragging = 0;
            return;
        }

        /* Maximize / restore. */
        if (cursor_x >= terminal_x_current + terminal_w - 96 &&
            cursor_x < terminal_x_current + terminal_w - 48 &&
            cursor_y >= terminal_y_current + 4 &&
            cursor_y < terminal_y_current + 42)
        {
            graphics_terminal_toggle_maximized();
            return;
        }

        /* Actual close button. */
        if (cursor_x >= terminal_x_current + terminal_w - 48 &&
            cursor_x < terminal_x_current + terminal_w &&
            cursor_y >= terminal_y_current + 4 &&
            cursor_y < terminal_y_current + 42)
        {
            terminal_close_requested = 1;
            terminal_running = 0;
            active_panel = 0;
            terminal_dragging = 0;
            return;
        }
        if (!terminal_maximized &&
            cursor_x >= terminal_x_current + 8 &&
            cursor_x < terminal_x_current + terminal_w - 140 &&
            cursor_y >= terminal_y_current + 4 &&
            cursor_y < terminal_y_current + 42)
        {
            terminal_dragging = 1;
            terminal_drag_offset_x = cursor_x - terminal_x;
            terminal_drag_offset_y = cursor_y - terminal_y;
            return;
        }

        return;
    }

    if (active_panel == 1)
    {
        int window_w = 760;
        int window_h = 480;
        int window_x = width / 2 - window_w / 2;
        int window_y = height / 2 - window_h / 2;

        if (cursor_x >= window_x + window_w - 52 &&
            cursor_x < window_x + window_w &&
            cursor_y >= window_y && cursor_y < window_y + 44)
        {
            active_panel = 0;
            return;
        }

        return;
    }

    if (active_panel == 4)
    {
        int window_w=width-32; if(window_w>760) window_w=760;
        int window_h=height-96; if(window_h>480) window_h=480;
        int window_x=width/2-window_w/2;
        int window_y=height/2-window_h/2;

        if (cursor_x>=window_x+window_w-52 && cursor_x<window_x+window_w &&
            cursor_y>=window_y && cursor_y<window_y+44)
        {
            active_panel=0;
            return;
        }

        if (cursor_y>=window_y+156 && cursor_y<window_y+210)
        {
            for(int i=0;i<3;i++)
            {
                int gap=10;
                int bw=(window_w-56-gap*2)/3;
                int bx=window_x+28+i*(bw+gap);
                if(cursor_x>=bx && cursor_x<bx+bw)
                {
                    settings_resolution_index=i;
                    graphics_set_resolution(settings_widths[i],settings_heights[i]);
                    return;
                }
            }
        }
        return;
    }

    if (active_panel == 2)
        return;

    if (cursor_x >= 24 && cursor_x < 112 &&
        cursor_y >= 26 && cursor_y < 108)
    {
        active_panel = 1;
        return;
    }

    if (cursor_x >= 120 && cursor_x < 208 &&
        cursor_y >= 26 && cursor_y < 108)
    {
        active_panel = 2;
        files_current_dir = filesystem_root();
        files_open_file = -1;
        return;
    }

    if (cursor_x >= 216 && cursor_x < 304 &&
        cursor_y >= 26 && cursor_y < 108)
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

    if (explorer_dragging && !explorer_maximized && active_panel == 2)
    {
        explorer_x = cursor_x - explorer_drag_offset_x;
        explorer_y = cursor_y - explorer_drag_offset_y;

        if (explorer_x < 0) explorer_x = 0;
        if (explorer_y < 0) explorer_y = 0;

        int ew = (int)framebuffer_width - 48;
        int eh = (int)framebuffer_height - 100;
        if (ew > 920) ew = 920;
        if (eh > 560) eh = 560;

        if (explorer_x + ew > (int)framebuffer_width)
            explorer_x = (int)framebuffer_width - ew;
        if (explorer_y + eh > (int)framebuffer_height)
            explorer_y = (int)framebuffer_height - eh;
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
}

void graphics_cursor_restore(void)
{
    if (!initialized || !cursor_saved_valid)
        return;

    for (int y = 0; y < cursor_saved_height; y++)
    {
        volatile uint32_t* row =
            (volatile uint32_t*)(framebuffer +
                (cursor_saved_y + y) * framebuffer_pitch);

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

    /* The logical pointer is the visual pointer. No host/window offset. */
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
            (volatile uint32_t*)(framebuffer +
                (cursor_saved_y + py) * framebuffer_pitch);

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
