/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/**
 * @brief Configures PIT channel 0 to fire at the requested frequency.
 * @return Nonzero on success; zero if the frequency cannot be represented.
 */
int timer_initialize(uint32_t frequency);

/**
 * @brief Called by IRQ0 handler in interrupts.c.
 */
void timer_handle_interrupt(void);

/**
 * @brief Blocks execution for the specified duration in milliseconds.
 * @note Requires maskable interrupts to remain enabled so timer ticks advance.
 */
void sleep_ms(uint32_t ms);

/**
 * @brief Returns total ticks since boot.
 */
uint32_t timer_get_ticks(void);

/** Returns the monotonic tick counter without the legacy 32-bit wrap. */
uint64_t timer_get_ticks64(void);

/** Returns the configured PIT frequency in ticks per second. */
uint32_t timer_get_frequency(void);

/** Returns monotonic milliseconds since timer initialization. */
uint64_t timer_get_uptime_ms(void);

#endif // TIMER_H