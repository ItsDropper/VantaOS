#include "graphics_internal.h"
#include "desktop_theme.h"
#include "filesystem.h"
#include "file_explorer.h"
#include "terminal.h"
#include "process.h"
#include "pmm.h"

void graphics_draw_vanta_logo(int x, int y, int size)
{
    /* Original Vanta mark: a clean geometric V built from two angled bars. */
    int half = size / 2;
    graphics_fill_rect(x + half - 3, y, 6, size, 0x003B82F6);
    graphics_fill_rect(x + half + 8, y, 6, size, 0x005AA9E6);
    graphics_fill_rect(x + half - 3, y + size - 8, 18, 8, 0x0048A8FF);
}

int graphics_files_window_x(void)
{
    return (int)framebuffer_width / 2 - 380;
}

int graphics_files_window_y(void)
{
    return (int)framebuffer_height / 2 - 240;
}

void graphics_draw_panel(int x, int y, int w, int h)
{
    /* Modern, low-contrast window surface with a restrained blue edge. */
    graphics_fill_rounded_rect(x + 7, y + 9, w, h, 12, 0x0006090F);
    graphics_fill_rounded_rect(x, y, w, h, 12, 0x00151B25);
    graphics_fill_rounded_rect(x, y, w, 46, 12, 0x001D2633);
    graphics_fill_rect(x + 1, y + 45, w - 2, 1, 0x00293443);
    graphics_fill_rect(x + 18, y + 45, 44, 2, VANTA_UI_ACCENT);
}


void graphics_draw_window_frame(int x, int y, int w, int h, const char* title, int show_window_controls)
{
    if (w <= 0 || h <= 0)
        return;

    /* Shared desktop window chrome: one border, one titlebar, one control grid. */
    graphics_fill_rounded_rect(x + 5, y + 7, w, h, 10, 0x0005080D);
    graphics_fill_rounded_rect(x, y, w, h, 10, VANTA_UI_SURFACE);
    graphics_fill_rounded_rect(x + 1, y + 1, w - 2, 45, 9, VANTA_UI_TITLEBAR);
    graphics_fill_rect(x + 1, y + 45, w - 2, 1, VANTA_UI_BORDER);

    graphics_fill_rounded_rect(x + 14, y + 10, 24, 24, 6, 0x00253648);
    graphics_draw_vanta_logo(x + 21, y + 14, 15);
    graphics_draw_text(x + 48, y + 18, title ? title : "Window",
                       VANTA_UI_TEXT, 1);

    if (show_window_controls)
    {
        int controls_x = x + w - 138;
        for (int i = 0; i < 3; i++)
        {
            int bx = controls_x + i * 46;
            graphics_fill_rect(bx, y + 1, 1, 44, VANTA_UI_BORDER);
        }
        graphics_draw_text(controls_x + 20, y + 19, "_", 0x00AAB8C8, 1);
        graphics_draw_text(controls_x + 60, y + 17, "[]", 0x00AAB8C8, 1);
        graphics_draw_text(controls_x + 110, y + 18, "X", 0x00DCE4EC, 1);
    }
    else
    {
        graphics_fill_rect(x + w - 46, y + 1, 1, 44, VANTA_UI_BORDER);
        graphics_draw_text(x + w - 28, y + 18, "X", 0x00DCE4EC, 1);
    }
}

void graphics_draw_taskbar(int taskbar_y, int width)
{
    int center = width / 2;

    /* Floating, compact dock. Coordinates intentionally match input hitboxes. */
    graphics_fill_rounded_rect(center - 202, taskbar_y + 5, 224, 54, 14,
        0x00151C27);
    graphics_fill_rect(center - 190, taskbar_y + 5, 200, 1, 0x002A3747);

    graphics_fill_rounded_rect(center - 190, taskbar_y + 10, 48, 42, 10,
        start_menu_open ? 0x002C3C50 : 0x001C2633);
    graphics_draw_vanta_logo(center - 180, taskbar_y + 17, 26);

    graphics_fill_rounded_rect(center - 132, taskbar_y + 10, 68, 42, 10,
        active_panel == 2 ? 0x00304459 : 0x001C2633);
    graphics_fill_rounded_rect(center - 112, taskbar_y + 20, 24, 18, 4,
        VANTA_UI_ACCENT);
    graphics_fill_rect(center - 108, taskbar_y + 17, 12, 5, 0x0079B8FF);
    if (active_panel == 2)
        graphics_fill_rounded_rect(center - 110, taskbar_y + 48, 24, 2, 1,
            0x0079B8FF);

    graphics_fill_rounded_rect(center - 56, taskbar_y + 10, 68, 42, 10,
        active_panel == 3 ? 0x00304459 : 0x001C2633);
    graphics_fill_rounded_rect(center - 40, taskbar_y + 19, 36, 24, 5,
        0x000B1119);
    graphics_draw_text(center - 34, taskbar_y + 26, ">_", 0x0079B8FF, 1);
    if (terminal_running)
        graphics_fill_rounded_rect(center - 30, taskbar_y + 48, 16, 2, 1,
            0x0079B8FF);

    /* No decorative status icon: every taskbar affordance has an action. */
}

void graphics_present(void)
{
    if (!initialized)
        return;

    graphics_cursor_restore();

    int w = (int)framebuffer_width;
    int h = (int)framebuffer_height;
    int taskbar_y = h - 64;

    graphics_draw_desktop_backdrop(w, h);
    if (active_panel == 0)
        graphics_draw_desktop_dashboard(w, h);

    if (active_panel == 4)
    {
        int ww = w - 32;
        if (ww > 760) ww = 760;
        int wh = h - 96;
        if (wh > 480) wh = 480;
        int wx = w / 2 - ww / 2, wy = h / 2 - wh / 2;
        graphics_draw_window_frame(wx, wy, ww, wh, "Settings", 0);
        graphics_draw_settings_page_detail(wx, wy, ww, wh);
    }

    if (active_panel == 1)
    {
        int ww = 760, wh = 480;
        int wx = w / 2 - ww / 2, wy = h / 2 - wh / 2;

        graphics_draw_window_frame(wx, wy, ww, wh, "System", 0);

        graphics_draw_system_page_detail(wx, wy, ww, wh);
    }

    if (active_panel == 2)
        file_explorer_draw(w, h);

    graphics_draw_taskbar(taskbar_y, w);

    if (active_panel == 3)
        return;

    if (start_menu_open)
    {
        int menu_w = 460, menu_h = 500;
        if (w < 492) menu_w = w - 32;
        if (h < 532) menu_h = h - 32;
        int mx = w / 2 - menu_w / 2;
        int my = h - menu_h - 8;

        graphics_fill_rounded_rect(mx + 7, my + 9, menu_w, menu_h, 14, 0x0006090F);
        graphics_fill_rounded_rect(mx, my, menu_w, menu_h, 14, 0x00151B25);
        graphics_fill_rounded_rect(mx, my, menu_w, 48, 14, 0x001D2633);
        graphics_fill_rect(mx + 1, my + 47, menu_w - 2, 1, 0x00293443);
        graphics_draw_vanta_logo(mx + 22, my + 14, 27);
        graphics_draw_text(mx + 62, my + 20, "VantaOS", 0x00F0F4F8, 1);
        graphics_draw_text(mx + 24, my + 76, "Pinned apps", 0x007D8DA1, 1);

        /* Consistent app rows: names, concise descriptions and distinct
           icon surfaces. The hitboxes in graphics_input.c match these rows. */
        graphics_fill_rounded_rect(mx + 24, my + 110, menu_w - 48, 50, 9, 0x001F2B38);
        graphics_fill_rounded_rect(mx + 36, my + 121, 28, 28, 7, 0x00202F40);
        graphics_draw_text(mx + 43, my + 132, ">_", 0x0079B8FF, 1);
        graphics_draw_text(mx + 76, my + 121, "Terminal", 0x00E5EBF2, 1);
        graphics_draw_text(mx + 76, my + 138, "Run commands and tools", 0x008A9BAD, 1);

        graphics_fill_rounded_rect(mx + 24, my + 164, menu_w - 48, 50, 9, 0x001F2B38);
        graphics_fill_rounded_rect(mx + 36, my + 175, 28, 28, 7, 0x00202F40);
        graphics_fill_rounded_rect(mx + 42, my + 184, 16, 12, 3, 0x0056B98A);
        graphics_fill_rect(mx + 44, my + 181, 8, 4, 0x0082D7AB);
        graphics_draw_text(mx + 76, my + 175, "Files", 0x00E5EBF2, 1);
        graphics_draw_text(mx + 76, my + 192, "Browse files and folders", 0x008A9BAD, 1);

        graphics_fill_rounded_rect(mx + 24, my + 218, menu_w - 48, 50, 9, 0x001F2B38);
        graphics_fill_rounded_rect(mx + 36, my + 229, 28, 28, 7, 0x00202F40);
        graphics_fill_rounded_rect(mx + 42, my + 235, 16, 16, 4, 0x003B82F6);
        graphics_draw_text(mx + 47, my + 240, "i", 0x00FFFFFF, 1);
        graphics_draw_text(mx + 76, my + 229, "System", 0x00E5EBF2, 1);
        graphics_draw_text(mx + 76, my + 246, "Kernel and service status", 0x008A9BAD, 1);

        graphics_fill_rounded_rect(mx + 24, my + 272, menu_w - 48, 50, 9, 0x001F2B38);
        graphics_fill_rounded_rect(mx + 36, my + 283, 28, 28, 7, 0x00202F40);
        graphics_fill_rounded_rect(mx + 43, my + 289, 14, 14, 3, 0x0093A3B5);
        graphics_fill_rect(mx + 49, my + 286, 3, 20, 0x0093A3B5);
        graphics_fill_rect(mx + 40, my + 295, 20, 3, 0x0093A3B5);
        graphics_draw_text(mx + 76, my + 283, "Settings", 0x00E5EBF2, 1);
        graphics_draw_text(mx + 76, my + 300, "Display and appearance", 0x008A9BAD, 1);
        graphics_fill_rect(mx + 24, my + menu_h - 50, menu_w - 48, 1, 0x00293443);
        graphics_draw_text(mx + 24, my + menu_h - 34, "Local session", 0x006E8094, 1);
    }

    graphics_draw_cursor();
}
