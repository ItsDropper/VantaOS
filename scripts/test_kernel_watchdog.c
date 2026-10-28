/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "watchdog.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t fake_ticks;
uint32_t timer_get_ticks(void) { return fake_ticks; }

int main(void)
{
    assert(kernel_watchdog_register(0U) == 0U);
    assert(kernel_watchdog_register(0x80000000U) == 0U);

    fake_ticks = 100U;
    kernel_watchdog_handle_t first = kernel_watchdog_register(10U);
    assert(first != 0U);
    assert(kernel_watchdog_status(first) == KERNEL_WATCHDOG_ACTIVE);

    kernel_watchdog_timer_tick(109U);
    assert(kernel_watchdog_status(first) == KERNEL_WATCHDOG_ACTIVE);
    fake_ticks = 109U;
    assert(kernel_watchdog_kick(first));
    kernel_watchdog_timer_tick(118U);
    assert(kernel_watchdog_status(first) == KERNEL_WATCHDOG_ACTIVE);
    kernel_watchdog_timer_tick(119U);
    assert(kernel_watchdog_status(first) == KERNEL_WATCHDOG_EXPIRED);
    assert(!kernel_watchdog_kick(first));
    assert(!kernel_watchdog_kick(first + 0x100U));

    assert(kernel_watchdog_unregister(first));
    assert(kernel_watchdog_status(first) == KERNEL_WATCHDOG_INVALID);
    kernel_watchdog_handle_t reused = kernel_watchdog_register(5U);
    assert(reused != 0U && reused != first);
    assert(kernel_watchdog_status(first) == KERNEL_WATCHDOG_INVALID);
    assert(kernel_watchdog_unregister(reused));
    assert(!kernel_watchdog_unregister(reused));

    fake_ticks = 0xFFFFFFFCU;
    kernel_watchdog_handle_t wrapped = kernel_watchdog_register(5U);
    assert(wrapped != 0U);
    kernel_watchdog_timer_tick(0U);
    assert(kernel_watchdog_status(wrapped) == KERNEL_WATCHDOG_ACTIVE);
    kernel_watchdog_timer_tick(1U);
    assert(kernel_watchdog_status(wrapped) == KERNEL_WATCHDOG_EXPIRED);
    assert(kernel_watchdog_unregister(wrapped));

    kernel_watchdog_handle_t handles[KERNEL_WATCHDOG_MAX_CLIENTS];
    for (uint32_t i = 0U; i < KERNEL_WATCHDOG_MAX_CLIENTS; ++i)
    {
        handles[i] = kernel_watchdog_register(100U);
        assert(handles[i] != 0U);
    }
    assert(kernel_watchdog_register(100U) == 0U);
    for (uint32_t i = 0U; i < KERNEL_WATCHDOG_MAX_CLIENTS; ++i)
        assert(kernel_watchdog_unregister(handles[i]));

    puts("VANTAOS_KERNEL_WATCHDOG_OK");
    return 0;
}
