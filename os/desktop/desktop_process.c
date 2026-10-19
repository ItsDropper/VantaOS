/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "desktop_process.h"

#include "desktop.h"
#include "graphics.h"
#include "process.h"
#include "mouse.h"
#include "terminal_process.h"
#include "file_explorer_process.h"
#include "usb.h"
#include "net.h"
#include "serial.h"
#include "interrupts.h"

static uint32_t desktop_pid;

void desktop_process_main(void)
{
    serial_write("VANTAOS_DESKTOP_INPUT_LOOP_ONLINE\n");
    uint32_t last_network_tick = interrupts_get_ticks();
    while (1)
    {
        /*
         * Keep latency-sensitive input and frame presentation ahead of
         * polled devices. A large network-poll budget here used to let a
         * burst of RX work monopolize the desktop task, making both pointer
         * motion and keyboard interaction appear delayed in QEMU.
         *
         * USB and networking are polled with bounded work after the UI step.
         * The network stack gets at most one unit of work per timer tick.
         */
        mouse_process_events();
        terminal_process_set_input_enabled(graphics_get_active_panel() == 3);
        desktop_update();
        file_explorer_process_step();

        usb_poll();
        uint32_t tick = interrupts_get_ticks();
        if (tick != last_network_tick)
        {
            last_network_tick = tick;
            (void)net_poll(1U);
        }
        __asm__ volatile ("hlt");
    }
}

void desktop_process_initialize(void)
{
    desktop_pid = 0;
    file_explorer_process_initialize();
}

int desktop_process_start(uint32_t parent_pid)
{
    if (desktop_pid != 0)
        return (int)desktop_pid;

    int pid =
        process_create_kernel(
            "desktop",
            parent_pid,
            desktop_process_main
        );

    if (pid < 0)
        return -1;

    desktop_pid = (uint32_t)pid;
    return pid;
}
