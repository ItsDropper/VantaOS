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
#include "hid_i2c.h"
#include "i2c.h"
#include "net.h"
#include "serial.h"
#include "interrupts.h"
#include "gui_server.h"
#include "timer.h"
#include "watchdog.h"

static uint32_t desktop_pid;
static volatile uint32_t desktop_heartbeat_tick;
static volatile uint64_t desktop_watchdog_handle;

static void desktop_i2c_initialize_worker(void)
{
    serial_write("VANTAOS_I2C_INIT_WORKER_ONLINE\n");
    i2c_initialize();
    serial_write("VANTAOS_I2C_INIT_WORKER_COMPLETE\n");
    process_terminate_current_with_status(0);
}

/*
 * Runs independently of the compositor. It can report a desktop task that
 * stops completing frames while timer interrupts and the scheduler continue.
 * It cannot recover a CPU that has stopped receiving interrupts entirely.
 */
static void desktop_watchdog_worker(void)
{
    uint8_t reported_stall = 0U;
    for (;;)
    {
        sleep_ms(1000U);
        uint64_t handle = __atomic_load_n(&desktop_watchdog_handle,
                                           __ATOMIC_ACQUIRE);
        if (handle == 0U)
            continue;

        kernel_watchdog_status_t status = kernel_watchdog_status(handle);
        if (status == KERNEL_WATCHDOG_EXPIRED && !reported_stall)
        {
            serial_write("VANTAOS_DESKTOP_WATCHDOG_EXPIRED\n");
            reported_stall = 1U;
        }
        if (reported_stall &&
            (uint32_t)(interrupts_get_ticks() -
                __atomic_load_n(&desktop_heartbeat_tick,
                                __ATOMIC_ACQUIRE)) <= 500U)
        {
            serial_write("VANTAOS_DESKTOP_WATCHDOG_RECOVERED\n");
            (void)kernel_watchdog_unregister(handle);
            uint64_t replacement = kernel_watchdog_register(500U);
            __atomic_store_n(&desktop_watchdog_handle, replacement,
                             __ATOMIC_RELEASE);
            reported_stall = 0U;
        }
    }
}

void desktop_process_main(void)
{
    serial_write("VANTAOS_DESKTOP_INPUT_LOOP_ONLINE\n");
    uint64_t watchdog = kernel_watchdog_register(500U);
    __atomic_store_n(&desktop_watchdog_handle, watchdog, __ATOMIC_RELEASE);
    if (watchdog == 0U)
        serial_write("VANTAOS_DESKTOP_WATCHDOG_REGISTER_FAILED\n");
    uint32_t last_network_tick = interrupts_get_ticks();
    uint32_t last_heartbeat_log = last_network_tick;
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
        hid_i2c_poll();
        mouse_process_events();
        int wheel_delta = mouse_get_wheel_delta();
        if (wheel_delta != 0)
            gui_server_mouse_wheel(wheel_delta);
        terminal_process_set_input_enabled(graphics_get_active_panel() == 3);
        desktop_update();
        file_explorer_process_step();

        usb_poll();
        uint32_t tick = interrupts_get_ticks();
        __atomic_store_n(&desktop_heartbeat_tick, tick, __ATOMIC_RELEASE);
        if (watchdog != 0U &&
            !kernel_watchdog_kick(watchdog))
        {
            watchdog = __atomic_load_n(&desktop_watchdog_handle,
                                        __ATOMIC_ACQUIRE);
            if (watchdog != 0U)
                (void)kernel_watchdog_kick(watchdog);
        }
        if ((uint32_t)(tick - last_heartbeat_log) >= 500U)
        {
            serial_write("VANTAOS_DESKTOP_HEARTBEAT_OK\n");
            last_heartbeat_log = tick;
        }
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
    __atomic_store_n(&desktop_heartbeat_tick, interrupts_get_ticks(),
                     __ATOMIC_RELEASE);

    int i2c_pid = process_create_kernel(
        "desktop-i2c-init", desktop_pid, desktop_i2c_initialize_worker);
    if (i2c_pid < 0)
        serial_write("VANTAOS_I2C_INIT_WORKER_CREATE_FAILED\n");

    int watchdog_pid = process_create_kernel(
        "desktop-watchdog", desktop_pid, desktop_watchdog_worker);
    if (watchdog_pid < 0)
        serial_write("VANTAOS_DESKTOP_WATCHDOG_CREATE_FAILED\n");
    return pid;
}
