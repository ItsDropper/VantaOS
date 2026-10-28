/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#ifndef TIMER_MATH_H
#define TIMER_MATH_H

#include <stdint.h>

#define VANTA_PIT_BASE_FREQUENCY 1193182U
#define VANTA_PIT_MIN_FREQUENCY  19U
#define VANTA_PIT_MAX_FREQUENCY  VANTA_PIT_BASE_FREQUENCY
#define VANTA_TIMER_MAX_WAIT_TICKS 0x7FFFFFFFU

/* Return zero for a frequency the PIT channel-0 divisor cannot represent
 * without using the special zero-as-65536 encoding. */
static inline int vanta_timer_pit_divisor(uint32_t frequency,
                                          uint16_t* divisor_out)
{
    if (divisor_out == 0 || frequency < VANTA_PIT_MIN_FREQUENCY ||
        frequency > VANTA_PIT_MAX_FREQUENCY)
        return 0;

    uint32_t divisor = VANTA_PIT_BASE_FREQUENCY / frequency;
    if (divisor == 0U || divisor > 0xFFFFU)
        return 0;

    *divisor_out = (uint16_t)divisor;
    return 1;
}

/* Round up so a nonzero requested delay never becomes a zero-tick delay. */
static inline uint64_t vanta_timer_ms_to_ticks(uint32_t milliseconds,
                                               uint32_t frequency)
{
    if (milliseconds == 0U || frequency == 0U)
        return 0U;
    return ((uint64_t)milliseconds * frequency + 999U) / 1000U;
}

/* Valid for intervals no longer than INT32_MAX ticks. */
static inline int vanta_timer_deadline_reached(uint32_t now,
                                               uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

/* Unsigned elapsed-time subtraction remains correct across uint32 wrap. */
static inline uint32_t vanta_timer_elapsed(uint32_t start, uint32_t now)
{
    return now - start;
}

#endif
