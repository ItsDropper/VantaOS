#include "graphics_internal.h"
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
        0x004B9CFF);
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

    /* Small system affordance at the right; not a fake status indicator. */
    graphics_fill_rounded_rect(width - 46, taskbar_y + 15, 30, 30, 9,
        0x0019232F);
    graphics_draw_vanta_logo(width - 42, taskbar_y + 19, 22);
}

void graphics_present(void)
{
    if (!initialized)
        return;

    graphics_cursor_restore();

    int w = (int)framebuffer_width;
    int h = (int)framebuffer_height;
    int taskbar_y = h - 64;

    /*
     * Vanta desktop direction: dark graphite surfaces, generous empty space,
     * muted blue highlights, and no fake live widgets. The original shortcut
     * hitboxes remain unchanged so mouse input continues to match the UI.
     */
    graphics_fill_rect(0, 0, w, h, 0x000B1018);
    graphics_fill_rect(0, 0, w, 32, 0x00101620);
    graphics_fill_rect(0, 31, w, 1, 0x00212D3A);
    graphics_fill_rect(0, 32, w, h - 96, 0x000D131C);

    /* Subtle geometric wallpaper, deliberately low contrast behind windows. */
    graphics_fill_rounded_rect(w - 360, 72, 300, 300, 150, 0x000F1C2B);
    graphics_fill_rounded_rect(w - 305, 120, 210, 210, 105, 0x00101B28);
    graphics_fill_rounded_rect(w - 250, 174, 100, 100, 50, 0x00131F2D);
    graphics_fill_rect(0, 32, 3, h - 96, 0x003D8DDF);

    /* Top bar: product identity and a clear workspace label. */
    graphics_draw_vanta_logo(18, 8, 17);
    graphics_draw_text(43, 12, "VANTAOS", 0x00E5EBF2, 1);
    graphics_draw_text(128, 12, "WORKSPACE", 0x007D8DA1, 1);
    graphics_fill_rounded_rect(w - 112, 8, 94, 17, 6, 0x001B2633);
    graphics_draw_text(w - 100, 13, "DESKTOP", 0x00AAB8C8, 1);

    /* App shortcuts retain their established input regions. */
    graphics_fill_rounded_rect(24, 26, 88, 82, 12,
        active_panel == 1 ? 0x00202D3B : 0x00101822);
    graphics_fill_rounded_rect(48, 43, 40, 34, 8, 0x00202F40);
    graphics_fill_rounded_rect(54, 49, 28, 22, 5, 0x004B9CFF);
    graphics_fill_rect(54, 46, 13, 4, 0x0079B8FF);
    graphics_draw_text(45, 87, "SYSTEM", 0x00D8E1EB, 1);

    graphics_fill_rounded_rect(120, 26, 88, 82, 12,
        active_panel == 2 ? 0x00202D3B : 0x00101822);
    graphics_fill_rounded_rect(144, 43, 40, 34, 8, 0x00202F40);
    graphics_fill_rounded_rect(150, 50, 28, 21, 5, 0x0056B98A);
    graphics_fill_rect(150, 47, 13, 4, 0x0082D7AB);
    graphics_draw_text(149, 87, "FILES", 0x00D8E1EB, 1);

    graphics_fill_rounded_rect(216, 26, 88, 82, 12,
        active_panel == 3 ? 0x00202D3B : 0x00101822);
    graphics_fill_rounded_rect(240, 43, 40, 34, 8, 0x00202F40);
    graphics_fill_rounded_rect(246, 48, 28, 24, 5, 0x000B1119);
    graphics_draw_text(250, 56, ">_", 0x0079B8FF, 1);
    graphics_draw_text(229, 87, "TERMINAL", 0x00D8E1EB, 1);

    graphics_draw_text(24, h - 88, "VANTAOS", 0x003D5268, 1);
    graphics_draw_text(24, h - 72, "LOCAL SESSION", 0x004A6075, 1);

    if (active_panel == 4)
    {
        int ww = w - 32;
        if (ww > 760) ww = 760;
        int wh = h - 96;
        if (wh > 480) wh = 480;
        int wx = w / 2 - ww / 2, wy = h / 2 - wh / 2;
        graphics_fill_rounded_rect(wx + 7, wy + 9, ww, wh, 14, 0x0006090F);
        graphics_fill_rounded_rect(wx, wy, ww, wh, 14, 0x00151B25);
        graphics_fill_rounded_rect(wx, wy, ww, 46, 14, 0x001D2633);
        graphics_fill_rect(wx + 1, wy + 45, ww - 2, 1, 0x00293443);
        graphics_draw_text(wx + 24, wy + 16, "SETTINGS", 0x00F0F4F8, 2);
        graphics_draw_text(wx + ww - 28, wy + 16, "X", 0x00D3DDE7, 1);
        graphics_draw_text(wx + 28, wy + 82, "DISPLAY", 0x0079B8FF, 1);
        graphics_draw_text(wx + 28, wy + 112, "RESOLUTION", 0x0093A3B5, 1);
        const char* labels[3] = {"800X600", "1024X768", "1280X720"};
        for (int i = 0; i < 3; i++)
        {
            int gap = 10;
            int bw = (ww - 56 - gap * 2) / 3;
            int bx = wx + 28 + i * (bw + gap);
            graphics_fill_rounded_rect(bx, wy + 140, bw, 52, 9,
                settings_resolution_index == i ? 0x002A5681 : 0x001D2936);
            if (settings_resolution_index == i)
                graphics_fill_rounded_rect(bx + 12, wy + 185, bw - 24, 2, 1,
                    0x0079B8FF);
            graphics_draw_text(bx + 14, wy + 160, labels[i], 0x00E5EBF2, 1);
        }
        graphics_draw_text(wx + 28, wy + 222, "DISPLAY MODE", 0x0093A3B5, 1);
        graphics_draw_text(wx + 190, wy + 222, "VBE FRAMEBUFFER", 0x00DCE5EE, 1);
        graphics_fill_rect(wx + 28, wy + 250, ww - 56, 1, 0x00293443);
        graphics_draw_text(wx + 28, wy + 274, "APPEARANCE", 0x0079B8FF, 1);
        graphics_draw_text(wx + 28, wy + 306, "Corner style", 0x0093A3B5, 1);
        graphics_draw_text(wx + 190, wy + 306, "Rounded", 0x00DCE5EE, 1);
        graphics_draw_text(wx + 28, wy + 354, "Changes apply immediately when supported.", 0x006E8094, 1);
    }

    if (active_panel == 1)
    {
        int ww = 760, wh = 480;
        int wx = w / 2 - ww / 2, wy = h / 2 - wh / 2;

        graphics_draw_panel(wx, wy, ww, wh);
        graphics_draw_vanta_logo(wx + 24, wy + 60, 30);
        graphics_draw_text(wx + 68, wy + 68, "SYSTEM", 0x00F0F4F8, 2);
        graphics_draw_text(wx + ww - 28, wy + 16, "X", 0x00D3DDE7, 1);

        graphics_draw_text(wx + 28, wy + 108, "SYSTEM OVERVIEW", 0x0079B8FF, 1);
        graphics_fill_rect(wx + 28, wy + 132, ww - 56, 1, 0x00293443);
        graphics_draw_text(wx + 28, wy + 154, "Architecture", 0x008A9BAD, 1);
        graphics_draw_text(wx + 190, wy + 154, "x86 32-bit", 0x00E0E7EF, 1);
        graphics_draw_text(wx + 28, wy + 180, "Kernel", 0x008A9BAD, 1);
        graphics_draw_text(wx + 190, wy + 180, "VantaOS", 0x00E0E7EF, 1);
        graphics_draw_text(wx + 28, wy + 206, "Display", 0x008A9BAD, 1);

        char resolution[32];
        unsigned int rw = (unsigned int)w, rh = (unsigned int)h;
        resolution[0]='R'; resolution[1]='E'; resolution[2]='S'; resolution[3]=' ';
        resolution[4]=(char)('0'+((rw/1000)%10));
        resolution[5]=(char)('0'+((rw/100)%10));
        resolution[6]=(char)('0'+((rw/10)%10));
        resolution[7]=(char)('0'+(rw%10));
        resolution[8]='x';
        resolution[9]=(char)('0'+((rh/1000)%10));
        resolution[10]=(char)('0'+((rh/100)%10));
        resolution[11]=(char)('0'+((rh/10)%10));
        resolution[12]=(char)('0'+(rh%10));
        resolution[13]=0;
        graphics_draw_text(wx + 190, wy + 206, resolution, 0x00E0E7EF, 1);

        graphics_draw_text(wx + 28, wy + 238, "MEMORY", 0x0079B8FF, 1);
        graphics_fill_rect(wx + 28, wy + 262, ww - 56, 1, 0x00293443);
        graphics_draw_text(wx + 28, wy + 282, "Filesystem", 0x008A9BAD, 1);
        graphics_draw_text(wx + 190, wy + 282,
            filesystem_is_initialized() ? "initialized" : "not initialized",
            0x00E0E7EF, 1);
        graphics_draw_text(wx + 28, wy + 308, "Processes", 0x008A9BAD, 1);
        graphics_draw_text(wx + 190, wy + 308,
            process_is_initialized() ? "process manager ready" : "not initialized",
            0x00E0E7EF, 1);

        graphics_draw_text(wx + 28, wy + 346, "SYSTEM PATHS", 0x0079B8FF, 1);
        graphics_fill_rect(wx + 28, wy + 370, ww - 56, 1, 0x00293443);
        graphics_draw_text(wx + 28, wy + 390, "/system/version", 0x00C5D1DE, 1);
        graphics_draw_text(wx + 28, wy + 412, "/system/kernel", 0x00C5D1DE, 1);
        graphics_draw_text(wx + 28, wy + 434, "/system/memory", 0x00C5D1DE, 1);
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
        graphics_draw_text(mx + 62, my + 20, "VANTAOS", 0x00F0F4F8, 1);
        graphics_draw_text(mx + 24, my + 76, "APPLICATIONS", 0x007D8DA1, 1);

        graphics_fill_rounded_rect(mx + 24, my + 110, menu_w - 48, 50, 9, 0x001F2B38);
        graphics_draw_text(mx + 42, my + 128, "TERMINAL", 0x00E5EBF2, 1);
        graphics_fill_rounded_rect(mx + 24, my + 164, menu_w - 48, 50, 9, 0x001F2B38);
        graphics_draw_text(mx + 42, my + 182, "FILES", 0x00E5EBF2, 1);
        graphics_fill_rounded_rect(mx + 24, my + 218, menu_w - 48, 50, 9, 0x001F2B38);
        graphics_draw_text(mx + 42, my + 236, "SYSTEM", 0x00E5EBF2, 1);
        graphics_fill_rounded_rect(mx + 24, my + 272, menu_w - 48, 50, 9, 0x001F2B38);
        graphics_draw_text(mx + 42, my + 290, "SETTINGS", 0x00E5EBF2, 1);
        graphics_draw_text(mx + 24, my + menu_h - 34, "BUILT-IN APPLICATIONS", 0x006E8094, 1);
    }

    graphics_draw_cursor();
}
