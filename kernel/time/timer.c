/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "timer.h"
#include "timer_math.h"

#define PIT_COMMAND_PORT 0x43
#define PIT_DATA_PORT_0  0x40

static volatile uint32_t ticks = 0;
static uint32_t timer_freq = 100; // Default 100 Hz

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

int timer_initialize(uint32_t frequency) {
    uint16_t divisor;
    if (!vanta_timer_pit_divisor(frequency, &divisor))
        return 0;
    timer_freq = frequency;

    // Command byte: Channel 0, Access mode lobyte/hibyte, Square wave mode
    outb(PIT_COMMAND_PORT, 0x36);

    // Send divisor bytes
    outb(PIT_DATA_PORT_0, (uint8_t)(divisor & 0xFF));
    outb(PIT_DATA_PORT_0, (uint8_t)((divisor >> 8) & 0xFF));
    return 1;
}

void timer_handle_interrupt(void) {
    __atomic_add_fetch(&ticks, 1U, __ATOMIC_RELAXED);
}

uint32_t timer_get_ticks(void) {
    return __atomic_load_n(&ticks, __ATOMIC_ACQUIRE);
}

void sleep_ms(uint32_t ms) {
    uint64_t remaining = vanta_timer_ms_to_ticks(ms, timer_freq);
    while (remaining != 0U) {
        uint32_t chunk = remaining > VANTA_TIMER_MAX_WAIT_TICKS ?
            VANTA_TIMER_MAX_WAIT_TICKS : (uint32_t)remaining;
        uint32_t start_tick = timer_get_ticks();

        while (vanta_timer_elapsed(start_tick, timer_get_ticks()) < chunk)
            __asm__ volatile ("hlt" ::: "memory");

        remaining -= chunk;
    }
}
