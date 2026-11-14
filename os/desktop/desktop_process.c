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
#include "keyboard.h"
#include "interrupt_health.h"
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
static uint32_t desktop_device_io_pid;
static uint32_t desktop_watchdog_pid;
static volatile uint32_t desktop_heartbeat_tick;
static volatile uint64_t desktop_watchdog_handle;
static uint64_t desktop_next_polled_device_tick;
static uint8_t desktop_polling_fallback_reported;

static void desktop_device_io_worker(void)
{
    serial_write("VANTAOS_DEVICE_IO_WORKER_ONLINE\n");
    i2c_initialize();
    serial_write("VANTAOS_DEVICE_IO_WORKER_READY\n");
    for (;;)
    {
        /* Keep potentially blocking polled bus transactions off the UI task. */
        hid_i2c_poll();
        usb_poll();
        sleep_ms(10U);
    }
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

static void desktop_start_background_workers(uint32_t parent_pid)
{
    if (parent_pid == 0U)
    {
        serial_write("VANTAOS_DESKTOP_WORKER_PARENT_INVALID\n");
        return;
    }

    if (desktop_device_io_pid == 0U)
    {
        int pid = process_create_kernel(
            "desktop-device-io", parent_pid, desktop_device_io_worker);
        if (pid < 0)
            serial_write("VANTAOS_DEVICE_IO_WORKER_CREATE_FAILED\n");
        else
            desktop_device_io_pid = (uint32_t)pid;
    }

    if (desktop_watchdog_pid == 0U)
    {
        int pid = process_create_kernel(
            "desktop-watchdog", parent_pid, desktop_watchdog_worker);
        if (pid < 0)
            serial_write("VANTAOS_DESKTOP_WATCHDOG_CREATE_FAILED\n");
        else
            desktop_watchdog_pid = (uint32_t)pid;
    }
}

void desktop_process_main(void)
{
    uint32_t current_pid = process_current_pid();
    if (desktop_pid == 0U)
        desktop_pid = current_pid;
    desktop_start_background_workers(current_pid);
    serial_write("VANTAOS_DESKTOP_INPUT_LOOP_ONLINE\n");
    uint64_t watchdog = kernel_watchdog_register(500U);
    __atomic_store_n(&desktop_watchdog_handle, watchdog, __ATOMIC_RELEASE);
    if (watchdog == 0U)
        serial_write("VANTAOS_DESKTOP_WATCHDOG_REGISTER_FAILED\n");
    else
        serial_write("VANTAOS_DESKTOP_WATCHDOG_REGISTERED\n");
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
         * Device-bus polling runs in its own process so slow USB/I2C
         * transactions do not hold up the compositor. Networking gets at
         * most one unit of work per timer tick.
         */
        mouse_process_events();
        int wheel_delta = mouse_get_wheel_delta();
        if (wheel_delta != 0)
            gui_server_mouse_wheel(wheel_delta);
        terminal_process_set_input_enabled(graphics_get_active_panel() == 3);
        desktop_update();
        file_explorer_process_step();

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
        /*
         * Drain the 8042 every pass so keyboard and touchpad input survive a
         * lost or mis-routed IRQ1/IRQ12 (an unread auxiliary byte would block
         * the keyboard behind it).
         */
        keyboard_poll_input();
        if (interrupt_health_polling_required())
        {
            /*
             * With no timer IRQ, the scheduler cannot run the background
             * device-I/O worker. Keep USB and I2C-HID alive from the active
             * desktop task using the TSC-backed monotonic clock instead of
             * sleeping on an interrupt that the health check proved absent.
             */
            uint64_t now = timer_get_ticks64();
            uint32_t frequency = timer_get_frequency();
            uint64_t poll_interval = frequency / 100U;
            if (poll_interval == 0U)
                poll_interval = 1U;
            if (timer_tsc_ticks_per_second() == 0U ||
                now >= desktop_next_polled_device_tick)
            {
                if (!desktop_polling_fallback_reported)
                {
                    serial_write("VANTAOS_DEVICE_IO_POLLING_FALLBACK\n");
                    desktop_polling_fallback_reported = 1U;
                }
                hid_i2c_poll();
                usb_poll();
                desktop_next_polled_device_tick = now + poll_interval;
            }
            __asm__ volatile ("pause" ::: "memory"); /* nothing could wake HLT */
        }
        else
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

    desktop_start_background_workers(desktop_pid);
    return pid;
}
