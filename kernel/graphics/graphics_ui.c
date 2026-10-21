/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "graphics_internal.h"
#include "gui_server.h"
#include "desktop_theme.h"
#include "filesystem.h"
#include "file_explorer.h"
#include "terminal.h"
#include "process.h"
#include "pmm.h"

void graphics_draw_start_menu_detail(int width, int height);

void graphics_draw_vanta_logo(int x, int y, int size)
{
    /* Compact electric-blue V mark; intentionally distinct from the Windows panes. */
    int steps = size / 2;
    if (steps < 2) steps = 2;
    for (int i = 0; i < steps; ++i)
    {
        int yy = y + i * 2;
        int inset = i * size / (steps * 2);
        uint32_t left = (i & 1) ? 0x005AA9E6U : 0x003B82F6U;
        uint32_t right = (i & 1) ? 0x0048A8FFU : 0x0067C5FFU;
        graphics_fill_rect(x + inset, yy, 3, 2, left);
        graphics_fill_rect(x + size - inset - 3, yy, 3, 2, right);
    }
    graphics_fill_rect(x + size / 2 - 2, y + size - 3, 5, 3, 0x0078B8FFU);
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
        /* Draw crisp native-size control glyphs instead of font approximations. */
        uint32_t control_ink = 0x00AAB8C8U;
        graphics_fill_rect(controls_x + 17, y + 25, 12, 2, control_ink);

        int maximize_x = controls_x + 46 + 17;
        graphics_fill_rect(maximize_x, y + 14, 12, 2, control_ink);
        graphics_fill_rect(maximize_x, y + 24, 12, 2, control_ink);
        graphics_fill_rect(maximize_x, y + 14, 2, 12, control_ink);
        graphics_fill_rect(maximize_x + 10, y + 14, 2, 12, control_ink);

        int close_x = controls_x + 2 * 46 + 23;
        uint32_t close_ink = 0x00DCE4ECU;
        for (int i = 0; i < 8; i++)
        {
            graphics_fill_rect(close_x - 3 + i, y + 15 + i, 2, 2, close_ink);
            graphics_fill_rect(close_x + 3 - i, y + 15 + i, 2, 2, close_ink);
        }
    }
    else
    {
        graphics_fill_rect(x + w - 46, y + 1, 1, 44, VANTA_UI_BORDER);
        graphics_draw_text(x + w - 28, y + 18, "X", 0x00DCE4EC, 1);
    }
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
    graphics_fill_rect(0, taskbar_y, width, 64, VANTA_UI_TOPBAR);
    graphics_fill_rect(0, taskbar_y, width, 1, VANTA_UI_BORDER);
    graphics_fill_rect(0, taskbar_y + 1, width, 1, VANTA_UI_SURFACE);

    /* Start */
    graphics_fill_rounded_rect(center - 108, taskbar_y + 10, 42, 42, 8,
        start_menu_open ? VANTA_UI_HOVER : VANTA_UI_TOPBAR);
    graphics_draw_vanta_logo(center - 97, taskbar_y + 20, 22);

    /* Search is styled like a native taskbar affordance; intentionally inert. */
    graphics_fill_rounded_rect(center - 58, taskbar_y + 10, 66, 42, 9,
                                VANTA_UI_SURFACE);
    graphics_fill_rounded_rect(center - 44, taskbar_y + 21, 12, 12, 6,
                                VANTA_UI_TEXT_SECONDARY);
    graphics_fill_rect(center - 34, taskbar_y + 31, 5, 2, VANTA_UI_TEXT_SECONDARY);
    graphics_draw_text(center - 26, taskbar_y + 27, "Search", VANTA_UI_TEXT_SECONDARY, 1);

    /* File Explorer: folder with a proper tab and body. */
    graphics_fill_rounded_rect(center + 18, taskbar_y + 10, 42, 42, 8,
        active_panel == 2 ? VANTA_UI_HOVER : VANTA_UI_TOPBAR);
    graphics_fill_rounded_rect(center + 27, taskbar_y + 24, 25, 16, 3,
                                0x00E9B94F);
    graphics_fill_rounded_rect(center + 28, taskbar_y + 20, 12, 6, 2,
                                0x00F5D77C);
    if (active_panel == 2)
        graphics_fill_rounded_rect(center + 29, taskbar_y + 49, 20, 2, 1,
                                    VANTA_UI_ACCENT);

    /* Terminal */
    graphics_fill_rounded_rect(center + 66, taskbar_y + 10, 42, 42, 8,
        active_panel == 3 ? VANTA_UI_HOVER : VANTA_UI_TOPBAR);
    graphics_fill_rounded_rect(center + 74, taskbar_y + 20, 26, 22, 5,
                                VANTA_UI_TITLEBAR);
    graphics_draw_text(center + 78, taskbar_y + 27, ">_", VANTA_UI_TEXT, 1);
    if (terminal_running)
        graphics_fill_rounded_rect(center + 74, taskbar_y + 49, 26, 2, 1,
                                    VANTA_UI_ACCENT);

    /* Give the clock its own high-contrast, padded region so both lines remain
       legible and inside the framebuffer at the 1024px CI screenshot width. */
    int clock_x = width - 116;
    if (clock_x < center + 116)
        clock_x = center + 116;

    /* Retained GUI windows get taskbar entries, including minimized windows. */
    gui_server_draw_taskbar_items(taskbar_y + 10, width);

    graphics_fill_rounded_rect(clock_x, taskbar_y + 6, width - clock_x - 8,
                                52, 8, VANTA_UI_SURFACE);
    graphics_fill_rect(clock_x + 7, taskbar_y + 8, 1, 48, VANTA_UI_BORDER);
    graphics_draw_text(clock_x + 15, taskbar_y + 17, clock, VANTA_UI_TEXT, 1);
    graphics_draw_text(clock_x + 15, taskbar_y + 34, date, VANTA_UI_TEXT_SECONDARY, 1);
}
void graphics_draw_start_menu_overlay(void)
{
    if (initialized && start_menu_open)
        graphics_draw_start_menu_detail((int)framebuffer_width,
                                        (int)framebuffer_height);
}

void graphics_present(void)
{
    if (!initialized)
        return;

    graphics_cursor_restore();

    int w = (int)framebuffer_width;
    int h = (int)framebuffer_height;
    int taskbar_y = h - 64;

    if (graphics_take_scene_invalidation())
    {
        graphics_draw_desktop_backdrop(w, h);
        if (active_panel == 0)
            graphics_draw_desktop_dashboard(w, h);
    }

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

}
