/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "timer_math.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_tsc_calibration(void)
{
    /* 3 GHz TSC over 11932 PIT counts (10 ms at 1,193,182 Hz). */
    uint64_t delta = 3000000000ULL / 100U;
    uint64_t per_tick = vanta_timer_tsc_per_tick(delta, 11932U, 100U);
    assert(per_tick > 29990000U && per_tick < 30010000U);
    assert(vanta_timer_tsc_to_ticks(per_tick * 250U, per_tick) == 250U);
    assert(vanta_timer_tsc_to_ticks(per_tick - 1U, per_tick) == 0U);
    assert(vanta_timer_tsc_to_ticks(12345U, 0U) == 0U);
    assert(vanta_timer_tsc_per_tick(0U, 11932U, 100U) == 0U);
    assert(vanta_timer_tsc_per_tick(delta, 0U, 100U) == 0U);
    assert(vanta_timer_tsc_per_tick(delta, 11932U, 0U) == 0U);
    assert(vanta_timer_tsc_per_tick(UINT64_MAX, 11932U, 100U) == 0U);
}

int main(void)
{
    uint16_t divisor = 0U;
    test_tsc_calibration();
    assert(vanta_timer_pit_divisor(100U, &divisor));
    assert(divisor == 11931U);
    assert(vanta_timer_pit_divisor(19U, &divisor));
    assert(divisor == 62799U);
    assert(vanta_timer_pit_divisor(VANTA_PIT_MAX_FREQUENCY, &divisor));
    assert(divisor == 1U);
    assert(!vanta_timer_pit_divisor(0U, &divisor));
    assert(!vanta_timer_pit_divisor(18U, &divisor));
    assert(!vanta_timer_pit_divisor(VANTA_PIT_MAX_FREQUENCY + 1U, &divisor));
    assert(!vanta_timer_pit_divisor(100U, 0));

    assert(vanta_timer_ms_to_ticks(0U, 100U) == 0U);
    assert(vanta_timer_ms_to_ticks(1U, 100U) == 1U);
    assert(vanta_timer_ms_to_ticks(10U, 100U) == 1U);
    assert(vanta_timer_ms_to_ticks(11U, 100U) == 2U);
    assert(vanta_timer_ms_to_ticks(UINT32_MAX, UINT32_MAX) > UINT32_MAX);
    assert(vanta_timer_ms_to_ticks(10U, 0U) == 0U);
    assert(vanta_timer_ms_to_ticks(52U, 19U) == 1U);
    assert(vanta_timer_ms_to_ticks(53U, 19U) == 2U);
    assert(vanta_timer_ms_to_ticks(UINT32_MAX, 100U) ==
           (((uint64_t)UINT32_MAX * 100U + 999U) / 1000U));

    assert(vanta_timer_ticks_to_ms(0U, 100U) == 0U);
    assert(vanta_timer_ticks_to_ms(1U, 100U) == 10U);
    assert(vanta_timer_ticks_to_ms(101U, 100U) == 1010U);
    assert(vanta_timer_ticks_to_ms(UINT64_MAX, 100U) == UINT64_MAX);
    assert(vanta_timer_ticks_to_ms(UINT64_MAX / 1000U, 1U) ==
           UINT64_MAX / 1000U * 1000U);
    assert(vanta_timer_ticks_to_ms(100U, 0U) == 0U);

    assert(vanta_timer_deadline_reached(100U, 100U));
    assert(vanta_timer_deadline_reached(101U, 100U));
    assert(!vanta_timer_deadline_reached(99U, 100U));
    assert(!vanta_timer_deadline_reached(0xFFFFFFFEU, 1U));
    assert(vanta_timer_deadline_reached(1U, 1U));
    assert(vanta_timer_elapsed(0xFFFFFFFEU, 1U) == 3U);

    assert(vanta_timer_deadline_after(100U, 25U) == 125U);
    assert(vanta_timer_deadline_after(UINT64_MAX - 2U, 1U) ==
           UINT64_MAX - 1U);
    assert(vanta_timer_deadline_after(UINT64_MAX - 2U, 3U) == UINT64_MAX);

    puts("VANTAOS_TIMER_MATH_OK");
    return 0;
}
