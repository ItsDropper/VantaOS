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

void graphics_draw_panel(int x, int y, int w, int h)
{
    /* Modern, low-contrast window surface with a restrained blue edge. */
    graphics_fill_rounded_rect(x + 7, y + 9, w, h, 12, 0x0006090F);
    graphics_fill_rounded_rect(x, y, w, h, 12, 0x00151B25);
    graphics_fill_rounded_rect(x, y, w, 46, 12, 0x001D2633);
    graphics_fill_rect(x + 1, y + 45, w - 2, 1, 0x00293443);
    graphics_fill_rect(x + 18, y + 45, 44, 2, 0x004B9CFF);
}


void graphics_draw_window_frame(int x, int y, int w, int h, const char* title)
{
    if (w <= 0 || h <= 0)
        return;

    /* Shared desktop window chrome: one border, one titlebar, one control grid. */
    graphics_fill_rounded_rect(x + 5, y + 7, w, h, 10, 0x0005080D);
    graphics_fill_rounded_rect(x, y, w, h, 10, 0x000F151E);
    graphics_fill_rounded_rect(x + 1, y + 1, w - 2, 45, 9, 0x001A2430);
    graphics_fill_rect(x + 1, y + 45, w - 2, 1, 0x002A3746);

    graphics_fill_rounded_rect(x + 14, y + 10, 24, 24, 6, 0x00253648);
    graphics_draw_vanta_logo(x + 21, y + 14, 15);
    graphics_draw_text(x + 48, y + 18, title ? title : "Window",
                       0x00E8EDF4, 1);

    int controls_x = x + w - 138;
    for (int i = 0; i < 3; i++)
    {
        int bx = controls_x + i * 46;
        graphics_fill_rect(bx, y + 1, 1, 44, 0x00232E3B);
    }
    graphics_draw_text(controls_x + 20, y + 19, "_", 0x00AAB8C8, 1);
    graphics_draw_text(controls_x + 60, y + 17, "[]", 0x00AAB8C8, 1);
    graphics_draw_text(controls_x + 110, y + 18, "X", 0x00DCE4EC, 1);
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

    /* Quiet, low-contrast workspace. App hitboxes stay stable while the
       presentation moves away from stacked legacy-style panels. */
    graphics_fill_rect(0, 0, w, h, 0x000C1119);
    graphics_fill_rect(0, 0, w, 42, 0x00151C26);
    graphics_fill_rect(0, 41, w, 1, 0x00232E3B);
    graphics_fill_rect(0, h - 64, w, 64, 0x00090D13);

    /* Restrained Vanta wallpaper mark, kept well behind application content. */
    graphics_fill_rounded_rect(w - 330, 112, 230, 230, 115, 0x000F1925);
    graphics_fill_rounded_rect(w - 286, 156, 142, 142, 71, 0x00121F2D);
    graphics_fill_rect(w - 218, 176, 14, 112, 0x00172A3D);
    graphics_fill_rect(w - 252, 210, 82, 14, 0x00172A3D);

    /* Compact workspace bar: identity left, session state right. */
    graphics_draw_vanta_logo(18, 11, 20);
    graphics_draw_text(48, 16, "VantaOS", 0x00E8EDF4, 1);
    graphics_fill_rect(112, 14, 1, 15, 0x00313D4B);
    graphics_draw_text(126, 16, "Workspace", 0x008D9BAC, 1);
    graphics_fill_rounded_rect(w - 126, 11, 106, 22, 7, 0x001F2935);
    graphics_draw_text(w - 111, 18, "Local session", 0x00C3CDD8, 1);

    /* Three restrained desktop shortcuts; click regions remain unchanged. */
    graphics_fill_rounded_rect(24, 58, 88, 72, 10,
        active_panel == 1 ? 0x00232F3D : 0x000F161F);
    graphics_fill_rounded_rect(49, 72, 38, 34, 8, 0x00202D3B);
    graphics_fill_rounded_rect(57, 80, 22, 18, 4, 0x004B9CFF);
    graphics_draw_text(43, 114, "System", 0x00DCE4EC, 1);

    graphics_fill_rounded_rect(120, 58, 88, 72, 10,
        active_panel == 2 ? 0x00232F3D : 0x000F161F);
    graphics_fill_rounded_rect(145, 72, 38, 34, 8, 0x00202D3B);
    graphics_fill_rounded_rect(151, 81, 26, 19, 4, 0x0057B77E);
    graphics_fill_rect(153, 77, 11, 5, 0x0085D9A9);
    graphics_draw_text(150, 114, "Files", 0x00DCE4EC, 1);

    graphics_fill_rounded_rect(216, 58, 88, 72, 10,
        active_panel == 3 ? 0x00232F3D : 0x000F161F);
    graphics_fill_rounded_rect(241, 72, 38, 34, 8, 0x00202D3B);
    graphics_draw_text(247, 84, ">_", 0x007DB8FF, 1);
    graphics_draw_text(228, 114, "Terminal", 0x00DCE4EC, 1);

    graphics_draw_text(24, h - 87, "VANTAOS", 0x004D6074, 1);
    graphics_draw_text(24, h - 71, "x86_64  /  LOCAL", 0x005D7185, 1);

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
        graphics_draw_text(wx + 24, wy + 16, "Settings", 0x00F0F4F8, 2);
        graphics_draw_text(wx + ww - 28, wy + 16, "X", 0x00D3DDE7, 1);
        graphics_draw_text(wx + 28, wy + 82, "Display", 0x0079B8FF, 1);
        graphics_draw_text(wx + 28, wy + 112, "Resolution", 0x0093A3B5, 1);
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
        graphics_draw_text(wx + 28, wy + 222, "Display mode", 0x0093A3B5, 1);
        graphics_draw_text(wx + 190, wy + 222, "VBE framebuffer", 0x00DCE5EE, 1);
        graphics_fill_rect(wx + 28, wy + 250, ww - 56, 1, 0x00293443);
        graphics_draw_text(wx + 28, wy + 274, "Appearance", 0x0079B8FF, 1);
        graphics_draw_text(wx + 28, wy + 306, "Window style", 0x0093A3B5, 1);
        graphics_draw_text(wx + 190, wy + 306, "Rounded corners", 0x00DCE5EE, 1);
        graphics_draw_text(wx + 28, wy + 354, "Display changes apply immediately when supported.", 0x006E8094, 1);
    }

    if (active_panel == 1)
    {
        int ww = 760, wh = 480;
        int wx = w / 2 - ww / 2, wy = h / 2 - wh / 2;

        graphics_draw_panel(wx, wy, ww, wh);
        graphics_draw_vanta_logo(wx + 24, wy + 60, 30);
        graphics_draw_text(wx + 68, wy + 68, "System", 0x00F0F4F8, 2);
        graphics_draw_text(wx + ww - 28, wy + 16, "X", 0x00D3DDE7, 1);

        graphics_draw_text(wx + 28, wy + 108, "System overview", 0x0079B8FF, 1);
        graphics_fill_rect(wx + 28, wy + 132, ww - 56, 1, 0x00293443);
        graphics_draw_text(wx + 28, wy + 154, "Architecture", 0x008A9BAD, 1);
        graphics_draw_text(wx + 190, wy + 154, "x86_64", 0x00E0E7EF, 1);
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

        graphics_draw_text(wx + 28, wy + 238, "SERVICES", 0x0079B8FF, 1);
        graphics_fill_rect(wx + 28, wy + 262, ww - 56, 1, 0x00293443);
        graphics_draw_text(wx + 28, wy + 282, "Filesystem", 0x008A9BAD, 1);
        graphics_draw_text(wx + 190, wy + 282,
            filesystem_is_initialized() ? "initialized" : "not initialized",
            0x00E0E7EF, 1);
        graphics_draw_text(wx + 28, wy + 308, "Processes", 0x008A9BAD, 1);
        graphics_draw_text(wx + 190, wy + 308,
            process_is_initialized() ? "process manager ready" : "not initialized",
            0x00E0E7EF, 1);

        graphics_draw_text(wx + 28, wy + 346, "System paths", 0x0079B8FF, 1);
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
