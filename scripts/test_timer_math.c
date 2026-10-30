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

int main(void)
{
    uint16_t divisor = 0U;
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
    assert(vanta_timer_ticks_to_ms(UINT64_MAX, 100U) ==
           (UINT64_MAX / 100U) * 1000U +
           ((UINT64_MAX % 100U) * 1000U) / 100U);
    assert(vanta_timer_ticks_to_ms(100U, 0U) == 0U);

    assert(vanta_timer_deadline_reached(100U, 100U));
    assert(vanta_timer_deadline_reached(101U, 100U));
    assert(!vanta_timer_deadline_reached(99U, 100U));
    assert(!vanta_timer_deadline_reached(0xFFFFFFFEU, 1U));
    assert(vanta_timer_deadline_reached(1U, 1U));
    assert(vanta_timer_elapsed(0xFFFFFFFEU, 1U) == 3U);

    puts("VANTAOS_TIMER_MATH_OK");
    return 0;
}
