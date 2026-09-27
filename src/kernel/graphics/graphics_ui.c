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
    graphics_fill_rect(x + 8, y + 10, w, h, 0x00000000);
    graphics_fill_rect(x, y, w, h, 0x00161E28);
    graphics_fill_rect(x, y, w, 44, 0x00212B37);
    graphics_fill_rect(x, y + 44, w, 1, 0x00334A60);
}

void graphics_draw_taskbar(int taskbar_y, int width)
{
    int center = width / 2;

    graphics_fill_rect(0, taskbar_y, width, 64, 0x00101822);
    graphics_fill_rect(0, taskbar_y, width, 1, 0x002B3B4C);

    /* Start / Vanta. */
    graphics_fill_rounded_rect(center - 190, taskbar_y + 8, 48, 48, 10, 0x001A2633);
    graphics_draw_vanta_logo(center - 180, taskbar_y + 18, 28);

    /* Files. */
    graphics_fill_rounded_rect(center - 132, taskbar_y + 8, 68, 48, 10,
        active_panel == 2 ? 0x00263B50 : 0x001A2633);
    graphics_fill_rect(center - 112, taskbar_y + 20, 26, 18, 0x005AA9E6);
    graphics_fill_rect(center - 108, taskbar_y + 17, 12, 5, 0x005AA9E6);
    graphics_draw_text(center - 98, taskbar_y + 46, "FILES", 0x00D8E2EA, 1);

    /* Terminal. */
    graphics_fill_rounded_rect(center - 56, taskbar_y + 8, 68, 48, 10,
        active_panel == 3 ? 0x00263B50 : 0x001A2633);
    graphics_fill_rect(center - 40, taskbar_y + 19, 36, 25, 0x000C141D);
    graphics_draw_text(center - 34, taskbar_y + 26, ">_", 0x005AA9E6, 1);

    if (terminal_running)
        graphics_fill_rect(center - 56, taskbar_y + 54, 68, 2, 0x00FFFFFF);

    graphics_draw_vanta_logo(width - 48, taskbar_y + 19, 24);
}

void graphics_present(void)
{
    if (!initialized)
        return;

    graphics_cursor_restore();

    int w = (int)framebuffer_width;
    int h = (int)framebuffer_height;
    int taskbar_y = h - 64;

    /* Restrained Vanta desktop: no fake widgets, only real app shortcuts. */
    /* Keep the framebuffer on the final background while the desktop is redrawn. */
    graphics_fill_rect(0, 0, w, h, 0x000D1823);
    graphics_fill_rect(0, 0, w, 2, 0x002B80C9);
    int side_w=w<860?w/2:420;
    graphics_fill_rect(0, 2, side_w, h - 66, 0x000E1C29);
    graphics_fill_rect(side_w, 2, 1, h - 66, 0x00142330);

    graphics_fill_rounded_rect(32, 34, 72, 58, 10, 0x00182A39);
    graphics_fill_rect(50, 49, 36, 25, 0x004B9CD3);
    graphics_fill_rect(50, 46, 15, 5, 0x004B9CD3);
    graphics_draw_text(43, 102, "SYSTEM", 0x00E7EEF4, 1);

    graphics_fill_rounded_rect(128, 34, 72, 58, 10, 0x00182A39);
    graphics_fill_rect(147, 49, 36, 25, 0x0057B77E);
    graphics_fill_rect(147, 46, 15, 5, 0x0057B77E);
    graphics_draw_text(146, 102, "FILES", 0x00E7EEF4, 1);

    graphics_fill_rounded_rect(224, 34, 72, 58, 10, 0x00182A39);
    graphics_fill_rect(242, 48, 38, 27, 0x00131D28);
    graphics_draw_text(249, 56, ">_", 0x005AA9E6, 1);
    graphics_draw_text(236, 102, "TERMINAL", 0x00E7EEF4, 1);

    graphics_draw_text(34, h - 92, "VANTAOS", 0x003E617A, 1);

    if (active_panel == 4)
    {
        int ww=w-32; if(ww>760) ww=760;
        int wh=h-96; if(wh>480) wh=480;
        int wx=w/2-ww/2, wy=h/2-wh/2;
        graphics_fill_rounded_rect(wx,wy,ww,wh,14,0x00161E28);
        graphics_fill_rounded_rect(wx,wy,ww,44,14,0x00212B37);
        graphics_draw_text(wx+24,wy+15,"SETTINGS",0x00FFFFFF,2);
        graphics_draw_text(wx+ww-28,wy+15,"X",0x00FFFFFF,2);
        graphics_draw_text(wx+28,wy+92,"DISPLAY",0x003B82F6,2);
        graphics_draw_text(wx+28,wy+126,"Resolution",0x00D8E2EA,1);
        const char* labels[3]={"800x600","1024x768","1280x720"};
        for(int i=0;i<3;i++)
        {
            int gap=10; int bw=(ww-56-gap*2)/3;
            int bx=wx+28+i*(bw+gap);
            graphics_fill_rounded_rect(bx,wy+156,bw,54,10,
                settings_resolution_index==i?0x002B80C9:0x00202C39);
            graphics_draw_text(bx+18,wy+177,labels[i],0x00FFFFFF,1);
        }
        graphics_draw_text(wx+28,wy+250,"Display mode",0x008EA0B3,1);
        graphics_draw_text(wx+190,wy+250,"VBE framebuffer",0x00F2F5F8,1);
        graphics_draw_text(wx+28,wy+286,"Appearance",0x003B82F6,2);
        graphics_draw_text(wx+28,wy+320,"Rounded corners",0x008EA0B3,1);
        graphics_draw_text(wx+190,wy+320,"ON",0x00F2F5F8,1);
        graphics_draw_text(wx+28,wy+360,"Resolution changes are applied immediately",0x008EA0B3,1);
        graphics_draw_text(wx+28,wy+382,"when the VBE display is available.",0x008EA0B3,1);
    }

    if (active_panel == 1)
    {
        int ww = 760, wh = 480;
        int wx = w / 2 - ww / 2, wy = h / 2 - wh / 2;

        graphics_draw_panel(wx, wy, ww, wh);
        graphics_draw_vanta_logo(wx + 24, wy + 62, 32);
        graphics_draw_text(wx + 72, wy + 70, "SYSTEM", 0x00FFFFFF, 2);
        graphics_draw_text(wx + ww - 28, wy + 15, "X", 0x00FFFFFF, 2);

        graphics_draw_text(wx + 28, wy + 120, "SYSTEM INFORMATION", 0x003B82F6, 2);
        graphics_draw_text(wx + 28, wy + 152, "Architecture", 0x008EA0B3, 1);
        graphics_draw_text(wx + 190, wy + 152, "x86 32-bit", 0x00F2F5F8, 1);
        graphics_draw_text(wx + 28, wy + 176, "Kernel", 0x008EA0B3, 1);
        graphics_draw_text(wx + 190, wy + 176, "VantaOS", 0x00F2F5F8, 1);
        graphics_draw_text(wx + 28, wy + 200, "Display", 0x008EA0B3, 1);

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

        graphics_draw_text(wx + 190, wy + 200, resolution, 0x00F2F5F8, 1);

        graphics_draw_text(wx + 28, wy + 236, "MEMORY", 0x003B82F6, 2);
        graphics_draw_text(wx + 28, wy + 268, "Total pages", 0x008EA0B3, 1);
        graphics_draw_text(wx + 190, wy + 268, "see /system/memory", 0x00F2F5F8, 1);
        graphics_draw_text(wx + 28, wy + 292, "Filesystem", 0x008EA0B3, 1);
        graphics_draw_text(wx + 190, wy + 292,
            filesystem_is_initialized() ? "initialized" : "not initialized",
            0x00F2F5F8, 1);
        graphics_draw_text(wx + 28, wy + 316, "Processes", 0x008EA0B3, 1);
        graphics_draw_text(wx + 190, wy + 316,
            process_is_initialized() ? "process manager ready" : "not initialized",
            0x00F2F5F8, 1);

        graphics_draw_text(wx + 28, wy + 352, "SYSTEM FILES", 0x003B82F6, 2);
        graphics_draw_text(wx + 28, wy + 382,
            "/system/version", 0x00F2F5F8, 1);
        graphics_draw_text(wx + 28, wy + 404,
            "/system/kernel", 0x00F2F5F8, 1);
        graphics_draw_text(wx + 28, wy + 426,
            "/system/memory", 0x00F2F5F8, 1);
    }

    if (active_panel == 2)
        file_explorer_draw(w, h);

    graphics_draw_taskbar(taskbar_y, w);

    if (active_panel == 3)
        return;

    if (start_menu_open)
    {
        int menu_w = 460, menu_h = 500;
        int mx = w / 2 - menu_w / 2;
        int my = h - menu_h - 8;

        graphics_fill_rect(mx + 8, my + 10, menu_w, menu_h, 0x00000000);
        graphics_fill_rounded_rect(mx, my, menu_w, menu_h, 14, 0x001A222D);
        graphics_fill_rect(mx, my, menu_w, 1, 0x003B82F6);

        graphics_draw_vanta_logo(mx + 24, my + 24, 34);
        graphics_draw_text(mx + 72, my + 32, "VANTAOS", 0x00FFFFFF, 2);
        graphics_draw_text(mx + 24, my + 78, "APPLICATIONS", 0x008EA0B3, 1);

        graphics_fill_rect(mx + 24, my + 110, 412, 54, 0x00212C3A);
        graphics_draw_text(mx + 42, my + 129, "TERMINAL", 0x00FFFFFF, 2);

        graphics_fill_rect(mx + 24, my + 164, 412, 54, 0x00212C3A);
        graphics_draw_text(mx + 42, my + 183, "FILES", 0x00FFFFFF, 2);

        graphics_fill_rounded_rect(mx + 24, my + 218, 412, 54, 10, 0x00212C3A);
        graphics_draw_text(mx + 42, my + 237, "SYSTEM", 0x00FFFFFF, 2);
        graphics_fill_rounded_rect(mx + 24, my + 272, 412, 54, 10, 0x00212C3A);
        graphics_draw_text(mx + 42, my + 291, "SETTINGS", 0x00FFFFFF, 2);

        graphics_draw_text(mx + 24, my + 460,
            "Built-in applications", 0x008EA0B3, 1);
    }

    graphics_draw_cursor();
}
