/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "timer.h"
#include "timer_math.h"
#include "watchdog.h"

#define PIT_COMMAND_PORT 0x43
#define PIT_DATA_PORT_0  0x40

static volatile uint64_t ticks = 0;
static volatile uint32_t timer_freq = 100; // Default 100 Hz

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
    uint64_t now = __atomic_add_fetch(&ticks, 1ULL, __ATOMIC_RELAXED);
    kernel_watchdog_timer_tick((uint32_t)now);
}

uint32_t timer_get_ticks(void) {
    return __atomic_load_n(&ticks, __ATOMIC_ACQUIRE);
}

uint64_t timer_get_ticks64(void) {
    return __atomic_load_n(&ticks, __ATOMIC_ACQUIRE);
}

uint32_t timer_get_frequency(void) {
    return __atomic_load_n(&timer_freq, __ATOMIC_ACQUIRE);
}

uint64_t timer_get_uptime_ms(void) {
    return vanta_timer_ticks_to_ms(timer_get_ticks64(),
                                   timer_get_frequency());
}

void sleep_ms(uint32_t ms) {
    uint64_t remaining = vanta_timer_ms_to_ticks(ms, timer_get_frequency());
    if (remaining == 0U)
        return;

    uint64_t start_tick = timer_get_ticks64();
    while (timer_get_ticks64() - start_tick < remaining)
        __asm__ volatile ("hlt" ::: "memory");
}
