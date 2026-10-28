/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>

#define KERNEL_WATCHDOG_MAX_CLIENTS 32U

typedef uint32_t kernel_watchdog_handle_t;

typedef enum
{
    KERNEL_WATCHDOG_INVALID = 0,
    KERNEL_WATCHDOG_ACTIVE = 1,
    KERNEL_WATCHDOG_EXPIRED = 2
} kernel_watchdog_status_t;

/* Timeout is measured in PIT ticks and must be in [1, INT32_MAX]. */
kernel_watchdog_handle_t kernel_watchdog_register(uint32_t timeout_ticks);
int kernel_watchdog_kick(kernel_watchdog_handle_t handle);
int kernel_watchdog_unregister(kernel_watchdog_handle_t handle);
kernel_watchdog_status_t kernel_watchdog_status(
    kernel_watchdog_handle_t handle);

/* Called from the timer interrupt. Never logs, allocates, or calls clients. */
void kernel_watchdog_timer_tick(uint32_t now);

#endif
