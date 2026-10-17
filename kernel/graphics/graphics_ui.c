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

static void taskbar_windows_logo(int x, int y, int size)
{
    int gap = size / 8;
    int pane = (size - gap) / 2;
    graphics_fill_rect(x, y, pane, pane, 0x004B8FE8);
    graphics_fill_rect(x + pane + gap, y, pane, pane, 0x004B8FE8);
    graphics_fill_rect(x, y + pane + gap, pane, pane, 0x004B8FE8);
    graphics_fill_rect(x + pane + gap, y + pane + gap, pane, pane,
                       0x004B8FE8);
}
static unsigned char ui_rtc_read(unsigned char reg)
{
    unsigned char value;
    __asm__ volatile ("outb %0, $0x70" : : "a"(reg));
    __asm__ volatile ("inb $0x71, %0" : "=a"(value));
    return value;
}
static unsigned char ui_bcd(unsigned char value)
{
    return (unsigned char)((value & 0x0F) + ((value >> 4) * 10));
}
static void ui_clock_text(char *clock, char *date)
{
    unsigned char second, minute, hour, day, month, year, status_b;
    /* Read a coherent RTC snapshot; the RTC stores calendar time in CMOS. */
    for (int tries = 0; tries < 4; ++tries)
    {
        for (unsigned int wait = 0; wait < 100000U; ++wait)
            if (!(ui_rtc_read(0x0A) & 0x80))
                break;
        second = ui_rtc_read(0x00);
        minute = ui_rtc_read(0x02);
        hour = ui_rtc_read(0x04);
        day = ui_rtc_read(0x07);
        month = ui_rtc_read(0x08);
        year = ui_rtc_read(0x09);
        status_b = ui_rtc_read(0x0B);
        if (second == ui_rtc_read(0x00))
            break;
    }
    if (!(status_b & 0x04))
    {
        second = ui_bcd(second);
        minute = ui_bcd(minute);
        hour = (unsigned char)((hour & 0x80) | ui_bcd(hour & 0x7F));
        day = ui_bcd(day);
        month = ui_bcd(month);
        year = ui_bcd(year);
    }
    if (!(status_b & 0x02))
    {
        unsigned char pm = hour & 0x80;
        hour &= 0x7F;
        hour = (unsigned char)(hour % 12);
        if (pm) hour = (unsigned char)(hour + 12);
    }
    clock[0] = (char)('0' + hour / 10);
    clock[1] = (char)('0' + hour % 10);
    clock[2] = ':';
    clock[3] = (char)('0' + minute / 10);
    clock[4] = (char)('0' + minute % 10);
    clock[5] = 0;
    /* Finnish-style date; RTC year is the conventional 2000-2099 range. */
    date[0] = (char)('0' + day / 10);
    date[1] = (char)('0' + day % 10);
    date[2] = '.';
    date[3] = (char)('0' + month / 10);
    date[4] = (char)('0' + month % 10);
    date[5] = '.';
    date[6] = '2';
    date[7] = '0';
    date[8] = (char)('0' + year / 10);
    date[9] = (char)('0' + year % 10);
    date[10] = 0;
}

void graphics_draw_taskbar(int taskbar_y, int width)
{
    int center = width / 2;
    char clock[6], date[11];
    ui_clock_text(clock, date);

    /* Windows 11-style taskbar: one quiet full-width surface and centered apps. */
    graphics_fill_rect(0, taskbar_y, width, 64, 0x00E6EDF6);
    graphics_fill_rect(0, taskbar_y, width, 1, 0x00CAD5E2);
    graphics_fill_rect(0, taskbar_y + 1, width, 1, 0x00F7FAFD);

    /* Start */
    graphics_fill_rounded_rect(center - 108, taskbar_y + 10, 42, 42, 8,
        start_menu_open ? 0x00C9DDF5 : 0x00E6EDF6);
    taskbar_windows_logo(center - 97, taskbar_y + 21, 20);

    /* Search is styled like a native taskbar affordance; intentionally inert. */
    graphics_fill_rounded_rect(center - 58, taskbar_y + 10, 66, 42, 9,
                                0x00F7F9FC);
    graphics_fill_rounded_rect(center - 44, taskbar_y + 21, 12, 12, 6,
                                0x005B7189);
    graphics_fill_rect(center - 34, taskbar_y + 31, 5, 2, 0x005B7189);
    graphics_draw_text(center - 26, taskbar_y + 27, "Search", 0x005B7189, 1);

    /* File Explorer: folder with a proper tab and body. */
    graphics_fill_rounded_rect(center + 18, taskbar_y + 10, 42, 42, 8,
        active_panel == 2 ? 0x00C9DDF5 : 0x00E6EDF6);
    graphics_fill_rounded_rect(center + 27, taskbar_y + 24, 25, 16, 3,
                                0x00E9B94F);
    graphics_fill_rounded_rect(center + 28, taskbar_y + 20, 12, 6, 2,
                                0x00F5D77C);
    if (active_panel == 2)
        graphics_fill_rounded_rect(center + 29, taskbar_y + 49, 20, 2, 1,
                                    0x004A8BE8);

    /* Terminal */
    graphics_fill_rounded_rect(center + 66, taskbar_y + 10, 42, 42, 8,
        active_panel == 3 ? 0x00C9DDF5 : 0x00E6EDF6);
    graphics_fill_rounded_rect(center + 74, taskbar_y + 20, 26, 22, 5,
                                0x002A3B50);
    graphics_draw_text(center + 78, taskbar_y + 27, ">_", 0x00F3F7FC, 1);
    if (terminal_running)
        graphics_fill_rounded_rect(center + 74, taskbar_y + 49, 26, 2, 1,
                                    0x004A8BE8);

    /* Give the clock its own high-contrast, padded region so both lines remain
       legible and inside the framebuffer at the 1024px CI screenshot width. */
    int clock_x = width - 116;
    if (clock_x < center + 116)
        clock_x = center + 116;
    graphics_fill_rounded_rect(clock_x, taskbar_y + 6, width - clock_x - 8,
                                52, 8, 0x00F7FAFD);
    graphics_fill_rect(clock_x + 7, taskbar_y + 8, 1, 48, 0x00D4DEEA);
    graphics_draw_text(clock_x + 15, taskbar_y + 17, clock, 0x001B2938, 1);
    graphics_draw_text(clock_x + 15, taskbar_y + 34, date, 0x0038495D, 1);
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

    graphics_draw_taskbar(taskbar_y, w);

    if (active_panel == 3)
        return;

    if (start_menu_open)
        graphics_draw_start_menu_detail(w, h);

}
