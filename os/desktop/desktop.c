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
#include "file_explorer.h"
#include "graphics_internal.h"
#include "gui_server.h"
#include "usb.h"
#include "input.h"
#include "xhci.h"

#include <stddef.h>
#include <stdint.h>

static int terminal_window_open;
static unsigned int desktop_last_clock_refresh;
static uint32_t desktop_last_drag_present_tick;
static void terminal_close_window(void);

static int desktop_hardware_diagnostics_active;
static uint32_t desktop_diagnostics_last_refresh;

static void desktop_diagnostics_hex(char *out, uint32_t value, unsigned int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    for (unsigned int i = 0; i < digits; ++i) {
        unsigned int shift = (digits - 1U - i) * 4U;
        out[i] = hex[(value >> shift) & 0x0FU];
    }
    out[digits] = '\\0';
}

static void desktop_draw_hardware_diagnostics(void)
{
    if (!graphics_is_initialized())
        return;
    uint32_t now = interrupts_get_ticks();
    if (desktop_diagnostics_last_refresh != 0U &&
        (uint32_t)(now - desktop_diagnostics_last_refresh) < 10U)
        return;
    desktop_diagnostics_last_refresh = now;

    graphics_begin_frame();
    if (!graphics_frame_is_active())
        return;

    const int width = (int)graphics_get_width();
    const int height = (int)graphics_get_height();
    graphics_fill_rect(0, 0, width, height, 0x00101824U);
    graphics_fill_rect(0, 0, width, 54, 0x001A2B43U);
    graphics_draw_text(24, 16, "VantaOS Hardware Diagnostics", 0x00FFFFFFU, 2U);
    graphics_draw_text(24, 68, "Keyboard: Ctrl + Shift + F9 toggles this screen",
                       0x009CB7D8U, 1U);

    graphics_draw_text(24, 104, "USB HOST CONTROLLER", 0x007DC4FFU, 1U);
    graphics_draw_text(24, 126,
        xhci_controller_online() ? "xHCI controller: ONLINE" :
                                   "xHCI controller: OFFLINE",
        xhci_controller_online() ? 0x007BE0A1U : 0x00FF8A8AU, 1U);

    uint32_t ports = xhci_port_count();
    char line[96];
    char hex[9];
    graphics_draw_text(24, 150, "ROOT PORTS (connected / enabled / status)",
                       0x00FFFFFFU, 1U);
    uint32_t hid_configured = 0U, hid_submitted = 0U;
    uint32_t hid_completed = 0U, hid_reports = 0U;
    uint32_t hid_mismatches = 0U, hid_in_flight = 0U;
    xhci_hid_diagnostic_counts(&hid_configured, &hid_submitted,
                               &hid_completed, &hid_reports,
                               &hid_mismatches, &hid_in_flight);
    desktop_diagnostics_hex(hex, hid_configured, 2U);
    graphics_draw_text(440, 126, "HID endpoints configured: 0x", 0x00C6D2E0U, 1U);
    graphics_draw_text(664, 126, hex, 0x00FFFFFFU, 1U);
    desktop_diagnostics_hex(hex, hid_submitted, 2U);
    graphics_draw_text(440, 143, "Transfers submitted: 0x", 0x00C6D2E0U, 1U);
    graphics_draw_text(624, 143, hex, 0x00FFFFFFU, 1U);
    desktop_diagnostics_hex(hex, hid_completed, 2U);
    graphics_draw_text(440, 160, "Completions matched: 0x", 0x00C6D2E0U, 1U);
    graphics_draw_text(624, 160, hex, 0x00FFFFFFU, 1U);
    desktop_diagnostics_hex(hex, hid_reports, 2U);
    graphics_draw_text(440, 177, "Reports received: 0x", 0x00C6D2E0U, 1U);
    graphics_draw_text(600, 177, hex, 0x00FFFFFFU, 1U);
    desktop_diagnostics_hex(hex, hid_mismatches, 2U);
    graphics_draw_text(440, 194, "Completion mismatches: 0x", 0x00C6D2E0U, 1U);
    graphics_draw_text(640, 194, hex, 0x00FFFFFFU, 1U);
    desktop_diagnostics_hex(hex, hid_in_flight, 2U);
    graphics_draw_text(440, 211, "Transfers in flight: 0x", 0x00C6D2E0U, 1U);
    graphics_draw_text(624, 211, hex, 0x00FFFFFFU, 1U);
    uint32_t visible_ports = ports > 16U ? 16U : ports;
    for (uint32_t port = 1U; port <= visible_ports; ++port) {
        uint32_t status = xhci_port_status(port);
        desktop_diagnostics_hex(hex, status, 8U);
        line[0] = 'P'; line[1] = '0' + (char)(port / 10U);
        line[2] = '0' + (char)(port % 10U); line[3] = ' ';
        const char *connected = (status & 1U) ? "connected" : "disconnected";
        const char *enabled = (status & 2U) ? "enabled" : "disabled";
        unsigned int at = 4U;
        while (*connected) line[at++] = *connected++;
        line[at++] = ' '; line[at++] = '/'; line[at++] = ' ';
        while (*enabled) line[at++] = *enabled++;
        line[at++] = ' '; line[at++] = '0'; line[at++] = 'x';
        for (unsigned int i = 0; i < 8U; ++i) line[at++] = hex[i];
        line[at] = '\\0';
        graphics_draw_text(32, 172 + (int)(port - 1U) * 17, line,
                           (status & 1U) ? 0x00B8D9C4U : 0x007D8B9BU, 1U);
    }
    if (ports == 0U)
        graphics_draw_text(32, 172, "No xHCI root ports reported",
                           0x00FF8A8AU, 1U);
    else if (ports > visible_ports)
        graphics_draw_text(32, 172 + (int)visible_ports * 17,
                           "Additional ports omitted", 0x007D8B9BU, 1U);

    int device_y = 172 + (int)(visible_ports ? visible_ports : 1U) * 17 + 12;
    if (device_y > height - 130)
        device_y = height - 130;
    graphics_draw_text(24, device_y, "REGISTERED INPUT DEVICES",
                       0x007DC4FFU, 1U);
    uint32_t device_count = input_device_count();
    uint32_t usb_keyboards = 0U, usb_mice = 0U;
    uint32_t i2c_pointers = 0U;
    uint32_t ps2_keyboards = 0U, ps2_mice = 0U;
    for (uint32_t i = 0U; i < device_count; ++i) {
        input_device_info_t device;
        if (!input_get_device(i, &device))
            continue;
        if (device.bus == INPUT_BUS_USB) {
            if (device.kind == INPUT_DEVICE_KEYBOARD) ++usb_keyboards;
            if (device.kind == INPUT_DEVICE_MOUSE) ++usb_mice;
        } else if (device.bus == INPUT_BUS_I2C) {
            if (device.kind == INPUT_DEVICE_MOUSE) ++i2c_pointers;
        } else if (device.bus == INPUT_BUS_PS2) {
            if (device.kind == INPUT_DEVICE_KEYBOARD) ++ps2_keyboards;
            if (device.kind == INPUT_DEVICE_MOUSE) ++ps2_mice;
        }
    }
    int row_y = device_y + 22;
    /* Compact, explicit counts avoid relying on a terminal or serial console. */
    graphics_draw_text(32, row_y + 18,
        "USB keyboard and mouse counts are listed in hexadecimal below",
        0x007D8B9BU, 1U);
    desktop_diagnostics_hex(hex, usb_keyboards, 2U);
    graphics_draw_text(32, row_y + 36, "USB keyboards: 0x", 0x00C6D2E0U, 1U);
    graphics_draw_text(160, row_y + 36, hex, 0x00FFFFFFU, 1U);
    desktop_diagnostics_hex(hex, usb_mice, 2U);
    graphics_draw_text(32, row_y + 53, "USB mice: 0x", 0x00C6D2E0U, 1U);
    graphics_draw_text(160, row_y + 53, hex, 0x00FFFFFFU, 1U);
    desktop_diagnostics_hex(hex, i2c_pointers, 2U);
    graphics_draw_text(32, row_y + 70, "I2C pointer devices: 0x", 0x00C6D2E0U, 1U);
    graphics_draw_text(208, row_y + 70, hex, 0x00FFFFFFU, 1U);
    desktop_diagnostics_hex(hex, ps2_keyboards, 2U);
    graphics_draw_text(32, row_y + 87, "PS/2 keyboards: 0x", 0x00C6D2E0U, 1U);
    graphics_draw_text(176, row_y + 87, hex, 0x00FFFFFFU, 1U);
    desktop_diagnostics_hex(hex, ps2_mice, 2U);
    graphics_draw_text(32, row_y + 104, "PS/2 mice: 0x", 0x00C6D2E0U, 1U);
    graphics_draw_text(160, row_y + 104, hex, 0x00FFFFFFU, 1U);

    graphics_draw_text(24, height - 42,
        "If USB mouse count is 0, inspect root-port state above. If I2C pointer is 0, touchpad discovery failed.",
        0x009CB7D8U, 1U);
    graphics_end_frame();
}

static int desktop_drag_present_due(void)
{
    uint32_t tick = interrupts_get_ticks();
    if ((uint32_t)(tick - desktop_last_drag_present_tick) < 2U)
        return 0;
    desktop_last_drag_present_tick = tick;
    return 1;
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
            if (graphics_set_resolution(settings_widths[i],
                                         settings_heights[i]))
            {
                settings_resolution_index = i;
                gui_server_reflow_windows();
            }
            return 1;
        }
    }
    return 0;
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
    (void)gui_server_register_builtin("Files",
        (screen_w - files_w) / 2, (work_h - files_h) / 2,
        files_w, files_h, desktop_draw_files, desktop_files_click, 0);
    /* Terminal is the ring-3 /TERMINAL.ELF window, not a second built-in. */
    (void)gui_server_register_builtin("System",
        (screen_w - app_w) / 2, (work_h - app_h) / 2,
        app_w, app_h, desktop_draw_system, 0, 0);
    int settings_x = (screen_w - app_w) / 2 + 24;
    int settings_y = (work_h - app_h) / 2 + 18;
    if (settings_x + app_w > screen_w) settings_x = screen_w - app_w;
    if (settings_y + app_h > work_h) settings_y = work_h - app_h;
    if (settings_x < 0) settings_x = 0;
    if (settings_y < 0) settings_y = 0;
    (void)gui_server_register_builtin("Settings",
        settings_x, settings_y, app_w, app_h,
        desktop_draw_settings, desktop_settings_click, 0);
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
 * A clock tick changes only the taskbar. Preserve the retained window pixels
 * and avoid snapshotting/replaying every window on each clock refresh.
 */
static void desktop_gui_present_clock_only(void)
{
    if (!graphics_is_initialized())
        return;

    graphics_begin_frame();
    if (!graphics_frame_is_active())
        return;

    /* The taskbar can overlap the cursor, so restore and redraw it safely. */
    graphics_cursor_restore();
    graphics_present();
    graphics_draw_cursor();
    graphics_end_frame();
}

/*
 * Terminal text changes do not alter the wallpaper or taskbar. Redraw the
 * retained windows on the compositor's shadow surface and commit only their
 * damaged tiles. Never paint the terminal directly into the scanout buffer.
 */
static void desktop_gui_present_windows_fast(void)
{
    (void)gui_server_take_window_redraw_request();
    gui_server_sync_active_panel();
    if (gui_server_is_window_dragging())
        return;
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
        graphics_select_panel(3);
        desktop_gui_present();
        return 1;
    }

    int pid = terminal_process_start(process_current_pid());

    if (pid < 0)
        return 0;

    terminal_window_open = 1;
    graphics_clear_terminal_close_requested();

    graphics_set_terminal_running(1);
    graphics_select_panel(3);
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
    desktop_gui_present();
}

void desktop_initialize(multiboot_info_t* mbd)
{
    (void)mbd;
    terminal_window_open = 0;
    desktop_last_clock_refresh = 0;
    (void)desktop_theme_reload();
    for (int i = 0; i < 3; ++i)
    {
        if (graphics_get_width() == settings_widths[i] &&
            graphics_get_height() == settings_heights[i])
        {
            settings_resolution_index = i;
            break;
        }
    }
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

    /* Ctrl+Shift+F9 opens diagnostics; the existing Ctrl+Super toggle remains. */
    int toggle_diagnostics = keyboard_take_f9_event() &&
        keyboard_ctrl_pressed() && keyboard_shift_pressed();
    if (keyboard_take_super_event())
    {
        if (keyboard_ctrl_pressed())
            toggle_diagnostics = 1;
        else if (!desktop_hardware_diagnostics_active)
        {
            graphics_toggle_start_menu();
            desktop_gui_present();
        }
    }
    if (toggle_diagnostics)
    {
        desktop_hardware_diagnostics_active =
            !desktop_hardware_diagnostics_active;
        desktop_diagnostics_last_refresh = 0U;
        graphics_invalidate_scene();
    }
    if (desktop_hardware_diagnostics_active)
    {
        desktop_draw_hardware_diagnostics();
        return;
    }

    /* Keep the RTC clock/date current even when the user is not moving the mouse. */
    if (graphics_get_active_panel() != 3)
    {
        unsigned int refresh = interrupts_get_ticks() / 50U;
        if (refresh != desktop_last_clock_refresh)
        {
            desktop_last_clock_refresh = refresh;
            desktop_gui_present_clock_only();
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
                desktop_gui_present_windows_fast();
            }
            /* The ring-3 terminal owns its cursor and redraws on input/output. */
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
            int builtin_dragging =
                graphics_terminal_is_dragging() ||
                graphics_explorer_is_dragging();
            if (click_event)
            {
                graphics_invalidate_scene();
                desktop_gui_present();
            }
            else if (builtin_dragging)
            {
                if (desktop_drag_present_due())
                {
                    graphics_invalidate_scene();
                    desktop_gui_present();
                }
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

    int window_dragging = gui_server_is_window_dragging();
    if (!window_dragging || desktop_drag_present_due())
    {
        if (gui_server_take_redraw_request())
        {
            graphics_invalidate_scene();
            desktop_gui_present();
        }
    }

    if (gui_server_take_window_redraw_request())
    {
        if (graphics_can_redraw_windows_incrementally() &&
            !gui_server_is_window_dragging())
            desktop_gui_present_windows_fast();
        else
        {
            graphics_invalidate_scene();
            desktop_gui_present();
        }
    }
}
