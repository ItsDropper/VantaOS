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
#include "gui_server.h"

#include <stddef.h>
#include <stdint.h>

static int terminal_window_open;
static unsigned int terminal_last_blink_state;
static unsigned int desktop_last_clock_refresh;


static void desktop_gui_present(void)
{
    if (!graphics_is_initialized())
        return;

    graphics_begin_frame();
    graphics_present();

    gui_server_draw_windows();
    graphics_draw_cursor();
    graphics_end_frame();
}

int desktop_launch_terminal(void)
{
    if (terminal_window_open)
        return 1;

    int pid = terminal_process_start(process_current_pid());

    if (pid < 0)
        return 0;

    terminal_window_open = 1;
    graphics_clear_terminal_close_requested();
    terminal_last_blink_state = interrupts_get_ticks() / 50U;

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
    terminal_last_blink_state = 0;
    desktop_last_clock_refresh = 0;
    (void)desktop_theme_reload();
}

void desktop_present(void)
{
    desktop_gui_present();
}

void desktop_update(void)
{
    gui_server_cleanup_dead_windows();

    uint32_t explorer_pid = file_explorer_process_pid();
    if (graphics_get_active_panel() == 2)
    {
        if (explorer_pid == 0U)
        {
            if (file_explorer_process_start(process_current_pid()) < 0)
                graphics_select_panel(0);
        }
        else
            (void)gui_server_set_owner_visible(explorer_pid, 1);
    }
    else if (explorer_pid != 0U)
        (void)gui_server_set_owner_visible(explorer_pid, 0);

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
                desktop_gui_present();
            }
            else
            {
                unsigned int blink_state = interrupts_get_ticks() / 50U;
                if (blink_state != terminal_last_blink_state)
                {
                    terminal_last_blink_state = blink_state;
                    desktop_gui_present();
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
                desktop_gui_present();
            else if (mouse_has_move_event())
                desktop_gui_present();
        }

        mouse_clear_event_flags();
    }

    if (gui_server_take_redraw_request())
        desktop_gui_present();
}
